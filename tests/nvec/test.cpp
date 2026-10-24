#include "haiku_stub.h"
#include <stdlib.h>
#include <assert.h>
extern "C" { void (*gArmPowerOffHook)(void) = NULL; }
area_id map_physical_memory(const char*, phys_addr_t, size_t, uint32, uint32, void** a)
	{ *a = calloc(1, 0x2000); return 1; }
#include "/home/atomozero/haiku-arm/haiku/src/add-ons/kernel/busses/i2c/nvec/nvec.cpp"

static uint8 regs[0x100];
static nvec_device* dev;

static uint8 irq(uint32 status, uint32 rcvd = 0)
{
	*(uint32*)(regs + I2C_SL_STATUS) = status;
	*(uint32*)(regs + I2C_SL_RCVD) = rcvd;
	nvec_interrupt(dev);
	return *(uint32*)(regs + I2C_SL_RCVD);
}

// the EC writes a message to us
static void ec_writes(const uint8* m, int n)
{
	irq(I2C_SL_IRQ | RCVD, 0x8a);
	for (int i = 0; i < n; i++)
		irq(I2C_SL_IRQ, m[i]);
	irq(I2C_SL_IRQ | END_TRANS);
}

// the EC reads our message, returns the bytes
static int ec_reads(uint8* out)
{
	irq(I2C_SL_IRQ | RCVD, 0x8a);
	irq(I2C_SL_IRQ, 0x01);
	int n = 0;
	out[n++] = irq(I2C_SL_IRQ | RNW | RCVD);
	uint32 len = out[0];
	for (uint32 i = 0; i < len; i++)
		out[n++] = irq(I2C_SL_IRQ | RNW);
	irq(I2C_SL_IRQ | RNW | END_TRANS);
	return n;
}

int main()
{
	dev = new nvec_device;
	memset((void*)dev, 0, sizeof(*dev));
	dev->registers = regs;
	dev->gpio = (volatile uint8*)calloc(1, 0x1000);
	dev->requestGPIO = 0xaa;
	dev->slaveAddress = 0x8a;
	dev->repeatDelay = 300000; dev->repeatRate = 35000; dev->clickSpeed = 500000;
	dev->rxSem = create_sem(0, ""); dev->txSem = create_sem(0, "");
	dev->transferSem = create_sem(0, ""); dev->keySem = create_sem(0, "");
	dev->mouseSem = create_sem(0, "");

	// keyboard: A down, A up, extended up arrow down
	const uint8 a[] = { 0x80, 0x1e }, b[] = { 0x80, 0x9e }, up[] = { 0xa0, 0xe0, 0x48 };
	ec_writes(a, 2); ec_writes(b, 2); ec_writes(up, 3);
	assert(gSems[dev->transferSem] == 0);	// events do not complete a transfer
	rx_thread(dev);
	raw_key_info k;
	read_key(dev, &k); printf("key %#x down %d\n", k.keycode, k.is_keydown);
	assert(k.keycode == kATKeycodeMap[0x1e - 1] && k.is_keydown);
	read_key(dev, &k); assert(k.keycode == kATKeycodeMap[0x1e - 1] && !k.is_keydown);
	read_key(dev, &k); printf("key %#x down %d\n", k.keycode, k.is_keydown);
	assert(k.keycode == kATKeycodeMap[0xc8 - 1] && k.is_keydown);
	// nothing left: the held up arrow repeats
	read_key(dev, &k); assert(k.keycode == kATKeycodeMap[0xc8 - 1] && k.is_keydown);
	printf("repeat ok\n");

	// the power button: a variable size event e0 5e e0 de, no key
	const uint8 power[] = { 0xc0, 4, 0xe0, 0x5e, 0xe0, 0xde };
	dev->repeatKey = 0;
	ec_writes(power, 6);
	rx_thread(dev);
	assert(dev->powerButtonPressed == 1 && dev->keyHead == dev->keyTail);
	nvec_handle button = { dev, NVEC_POWER_BUTTON };
	uint8 pressed = 0;
	size_t length = 1;
	assert(nvec_read(&button, 0, &pressed, &length) == B_OK && length == 1
		&& pressed == 1);
	length = 1;
	assert(nvec_read(&button, 0, &pressed, &length) == B_OK && pressed == 0);
	printf("power button ok\n");
	// the backlight keys change the PWM, they are no keys either
	static uint32 pwm = TEGRA_PWM_ENABLE | (128 << TEGRA_PWM_DUTY_SHIFT) | 3;
	dev->pwm = &pwm;
	dev->backlightLevel = 8;
	const uint8 brighter[] = { 0xa0, 0xe0, 0x39 }, darker[] = { 0xa0, 0xe0, 0x3a };
	ec_writes(brighter, 3); ec_writes(brighter, 3);
	rx_thread(dev);
	assert(dev->backlightLevel == 10 && dev->keyHead == dev->keyTail);
	assert((pwm & TEGRA_PWM_DUTY_MASK) >> TEGRA_PWM_DUTY_SHIFT == 160
		&& (pwm & 0xffff) == 3);
	for (int i = 0; i < 20; i++) {
		ec_writes(darker, 3);
		rx_thread(dev);
	}
	assert(dev->backlightLevel == 1);
	printf("backlight ok, PWM %#x\n", pwm);
	// F7 and F6 are the backlight keys of the AC100
	const uint8 f7[] = { 0x80, 0x41 }, f6[] = { 0x80, 0x40 };
	ec_writes(f7, 2); rx_thread(dev); ec_writes(f7, 2); rx_thread(dev);
	assert(dev->backlightLevel == 3 && dev->keyHead == dev->keyTail);
	ec_writes(f6, 2); rx_thread(dev);
	assert(dev->backlightLevel == 2);
	// the lid switches the backlight off and on again
	const uint8 closed[] = { 0xc5, 4, 0, 0, 2, 0 }, open[] = { 0xc5, 4, 0, 0, 0, 0 };
	ec_writes(closed, 6); rx_thread(dev);
	assert(dev->lidClosed && (pwm & (TEGRA_PWM_DUTY_MASK | TEGRA_PWM_ENABLE)) == 0);
	ec_writes(f7, 2); rx_thread(dev);
	assert((pwm & TEGRA_PWM_ENABLE) == 0);
	ec_writes(open, 6); rx_thread(dev);
	assert(!dev->lidClosed && (pwm & TEGRA_PWM_ENABLE) != 0
		&& (pwm & TEGRA_PWM_DUTY_MASK) >> TEGRA_PWM_DUTY_SHIFT == 3 * 16);
	printf("F6/F7 and lid ok, PWM %#x\n", pwm);

	// sending: the EC reads [len, type, cmd], then answers
	const uint8 enable[] = { NVEC_KBD, KBD_ENABLE };
	nvec_write(dev, enable, 2);
	uint8 out[40];
	int n = ec_reads(out);
	printf("sent:"); for (int i = 0; i < n; i++) printf(" %02x", out[i]); printf("\n");
	assert(n == 3 && out[0] == 2 && out[1] == NVEC_KBD && out[2] == KBD_ENABLE);
	const uint8 answer[] = { NVEC_KBD, 2, KBD_ENABLE, 0 };
	ec_writes(answer, 4);
	assert(gSems[dev->transferSem] == 1);
	printf("answer completes the transfer\n");

	// nothing queued: a no-op
	dev->txHead = dev->txTail;
	n = ec_reads(out);
	assert(n == 3 && out[0] == 2 && out[1] == NVEC_CNTL);
	printf("no-op ok\n");

	// touchpad: left button, x +5, y -5
	const uint8 ps2[] = { 0xc1, 3, 0x29, 0x05, 0xfb };
	ec_writes(ps2, 5);
	rx_thread(dev);
	mouse_movement m;
	assert(read_movement(dev, &m) == B_OK);
	printf("mouse buttons %u x %d y %d clicks %d\n", m.buttons, m.xdelta, m.ydelta, m.clicks);
	assert(m.buttons == 1 && m.xdelta == 5 && m.ydelta == -5 && m.clicks == 1);

	// the self test result after a reset must not shift the packets
	const uint8 reset[] = { 0xc1, 8, 0xaa, 0x00, 0x28, 0x00, 0xff, 0x18, 0xfa, 0x00 };
	ec_writes(reset, 10);
	rx_thread(dev);
	assert(read_movement(dev, &m) == B_OK);
	printf("after reset: x %d y %d\n", m.xdelta, m.ydelta);
	assert(m.xdelta == 0 && m.ydelta == -1);
	assert(read_movement(dev, &m) == B_OK);
	assert(m.xdelta == -6 && m.ydelta == 0);
	// and the touchpad gets enabled again: defaults and enable queued
	assert(dev->touchpadResets == 1 && dev->txTail - dev->txHead == 2);
	assert(dev->txMessages[dev->txHead % NVEC_TX_COUNT].data[3] == 0xf6);
	dev->txHead = dev->txTail;
	// "aa 00" inside a packet (x = 0xaa, y = 0) is no reset
	const uint8 inside[] = { 0xc1, 3, 0x08, 0xaa, 0x00 };
	ec_writes(inside, 5);
	rx_thread(dev);
	assert(read_movement(dev, &m) == B_OK && m.xdelta == (int32)0xaa
		&& dev->touchpadResets == 1 && dev->txTail == dev->txHead);
	printf("touchpad reset handling ok\n");

	// firmware version answer gets logged
	const uint8 fw[] = { NVEC_CNTL, 6, GET_FIRMWARE_VERSION, 0, 0x01, 0x02, 0x03, 0x04 };
	ec_writes(fw, 8);
	rx_thread(dev);
	printf("errors %u\n", dev->protocolErrors);
	assert(dev->protocolErrors == 0);
	// battery: present and discharging at 80 %, 7400 mV, -500 mA, full 2000 mAh
	const uint8 slot[] = { NVEC_BAT, 4, BAT_SLOT_STATUS, 0, 0x05, 80 };
	const uint8 volt[] = { NVEC_BAT, 4, BAT_VOLTAGE, 0, 0xe8, 0x1c };
	const uint8 cur[] = { NVEC_BAT, 4, BAT_CURRENT, 0, 0x0c, 0xfe };
	const uint8 full[] = { NVEC_BAT, 4, BAT_LAST_FULL_CHARGE_CAPACITY, 0, 0xd0, 0x07 };
	const uint8 model[] = { NVEC_BAT, 6, BAT_MODEL, 0, 'P', 'A', '3', '8' };
	const uint8 ac[] = { NVEC_SYS, 4, GET_SYSTEM_STATUS, 0, 1, 0 };
	ec_writes(slot, 6); ec_writes(volt, 6); ec_writes(cur, 6); ec_writes(full, 6);
	ec_writes(model, 8); ec_writes(ac, 6);
	rx_thread(dev);
	// the first "present" asks the EC for the battery details
	assert(dev->txTail - dev->txHead == 6);
	acpi_battery_info bi;
	acpi_extended_battery_info be;
	assert(battery_control(dev, GET_BATTERY_INFO, &bi) == B_OK);
	assert(battery_control(dev, GET_EXTENDED_BATTERY_INFO, &be) == B_OK);
	printf("battery state %u rate %u capacity %u/%u voltage %u model %s ac %d\n",
		bi.state, bi.current_rate, bi.capacity, be.last_full_charge, bi.voltage,
		be.model_number, dev->acOnline);
	assert(bi.state == BATTERY_DISCHARGING && bi.current_rate == 500
		&& bi.capacity == 1600 && bi.voltage == 7400 && be.last_full_charge == 2000
		&& strcmp(be.model_number, "PA38") == 0 && dev->acOnline);
	uint32 magic;
	assert(battery_control(dev, IDENTIFY_DEVICE, &magic) == B_OK && magic == kMagicACPIBatteryID);

	printf("ALL OK\n");
}
