#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
typedef int32_t int32; typedef uint32_t uint32; typedef uint16_t uint16; typedef uint8_t uint8;
typedef int64_t int64; typedef uint64_t uint64; typedef int16_t int16; typedef int8_t int8;
typedef int32 status_t; typedef int64 bigtime_t; typedef int32 area_id; typedef int32 sem_id;
typedef int32 thread_id; typedef uint64 phys_addr_t;
#define B_OK 0
#define B_ERROR (-1)
#define B_NO_MEMORY (-2)
#define B_BAD_VALUE (-3)
#define B_TIMED_OUT (-4)
#define B_WOULD_BLOCK (-5)
#define B_BAD_DATA (-6)
#define B_NOT_ALLOWED (-7)
#define B_BAD_ADDRESS (-8)
#define B_DEV_INVALID_IOCTL (-9)
#define B_NO_MORE_SEMS (-10)
#define B_HANDLED_INTERRUPT 1
#define B_UNHANDLED_INTERRUPT 0
#define B_PAGE_SIZE 4096
#define B_ANY_KERNEL_ADDRESS 0
#define B_KERNEL_READ_AREA 1
#define B_KERNEL_WRITE_AREA 2
#define B_DO_NOT_RESCHEDULE 0
#define B_RELATIVE_TIMEOUT 8
#define B_CAN_INTERRUPT 1
#define B_INFINITE_TIMEOUT INT64_MAX
#define B_URGENT_DISPLAY_PRIORITY 20
#define B_STRING_TYPE 0
#define B_DEVICE_PRETTY_NAME "device/pretty name"
#define B_DEVICE_MANAGER_MODULE_NAME "dm"
#define B_PRIx32 "x"
#define B_PRId32 "d"
#define B_PRIu32 "u"
#define B_PRIx64 "llx"
#define B_PRIu64 "llu"
#define B_COUNT_OF(a) (sizeof(a) / sizeof(a[0]))
#define B_BENDIAN_TO_HOST_INT32(x) __builtin_bswap32(x)
#define B_HOST_TO_LENDIAN_INT32(x) (x)
#define IS_USER_ADDRESS(x) true
#define B_SPINLOCK_INITIALIZER 0
typedef int spinlock;
struct InterruptsSpinLocker { InterruptsSpinLocker(spinlock&) {} void Unlock() {} };
typedef struct { int locked; } mutex;
static inline void mutex_init(mutex* m, const char*) { m->locked = 0; }
static inline void mutex_destroy(mutex*) {}
struct MutexLocker { MutexLocker(mutex&) {} void Unlock() {} };
struct select_sync_pool; struct selectsync;
#define B_SELECT_READ 1
static int gSelectNotified = 0;
static inline status_t notify_select_event_pool(select_sync_pool*, uint8) { gSelectNotified++; return 0; }
static inline status_t notify_select_event(selectsync*, uint8) { gSelectNotified++; return 0; }
static inline status_t add_select_sync_pool_entry(select_sync_pool** p, selectsync*, uint8) { *p = (select_sync_pool*)1; return 0; }
static inline status_t remove_select_sync_pool_entry(select_sync_pool** p, selectsync*, uint8) { *p = NULL; return 0; }
#define dprintf printf
static inline void spin(bigtime_t) {}
static bigtime_t gNow = 0;
static inline bigtime_t system_time() { return gNow; }
static int gSems[32]; static int gSemCount = 0;
static inline sem_id create_sem(int32 c, const char*) { gSems[gSemCount] = c; return gSemCount++; }
static inline status_t delete_sem(sem_id) { return 0; }
static inline status_t release_sem_etc(sem_id s, int32 c, uint32) { gSems[s] += c; return 0; }
static inline status_t release_sem(sem_id s) { gSems[s]++; return 0; }
static inline status_t acquire_sem_etc(sem_id s, int32, uint32, bigtime_t) { if (gSems[s] > 0) { gSems[s]--; return 0; } return B_TIMED_OUT; }
static inline status_t acquire_sem(sem_id s) { return acquire_sem_etc(s, 1, 0, 0); }
static inline status_t get_sem_count(sem_id s, int32* c) { *c = gSems[s]; return 0; }
static inline status_t user_memcpy(void* d, const void* s, size_t n) { memcpy(d, s, n); return 0; }
static inline area_id delete_area(area_id) { return 0; }
area_id map_physical_memory(const char*, phys_addr_t, size_t, uint32, uint32, void**);
typedef int32 (*interrupt_handler)(void*);
static inline status_t install_io_interrupt_handler(int32, interrupt_handler, void*, uint32) { return 0; }
static inline status_t remove_io_interrupt_handler(int32, interrupt_handler, void*) { return 0; }
typedef status_t (*thread_func)(void*);
static inline thread_id spawn_kernel_thread(thread_func, const char*, int32, void*) { return 1; }
static inline status_t resume_thread(thread_id) { return 0; }
static inline status_t wait_for_thread(thread_id, status_t*) { return 0; }
static inline status_t snooze(bigtime_t t) { gNow += t; return 0; }
struct device_node; struct fdt_device;
struct module_info { const char* name; uint32 flags; status_t (*std_ops)(int32, ...); };
struct device_attr { const char* name; uint32 type; union { const char* string; uint32 ui32; } value; };
struct driver_module_info { module_info info; float (*supports_device)(device_node*); status_t (*register_device)(device_node*);
	status_t (*init_driver)(device_node*, void**); void (*uninit_driver)(void*); status_t (*register_child_devices)(void*);
	status_t (*rescan_child_devices)(void*); void (*device_removed)(void*); };
struct device_module_info { module_info info; status_t (*init_device)(void*, void**); void (*uninit_device)(void*); void (*device_removed)(void*);
	status_t (*open)(void*, const char*, int, void**); status_t (*close)(void*); status_t (*free)(void*);
	status_t (*read)(void*, off_t, void*, size_t*); status_t (*write)(void*, off_t, const void*, size_t*);
	status_t (*io)(void*, void*); status_t (*control)(void*, uint32, void*, size_t); status_t (*select)(void*, uint8, selectsync*); status_t (*deselect)(void*, uint8, selectsync*); };
struct module_dependency { const char* name; module_info** info; };
struct device_manager_info { status_t (*get_attr_string)(device_node*, const char*, const char**, bool);
	status_t (*register_node)(device_node*, const char*, const device_attr*, void*, device_node**);
	device_node* (*get_parent_node)(device_node*); void (*put_node)(device_node*);
	status_t (*get_driver)(device_node*, driver_module_info**, void**);
	status_t (*publish_device)(device_node*, const char*, const char*); };
struct fdt_device_module_info { driver_module_info info; void* get_bus; void* get_name;
	const void* (*get_prop)(fdt_device*, const char*, int*); bool (*get_reg)(fdt_device*, uint32, uint64*, uint64*);
	bool (*get_interrupt)(fdt_device*, uint32, device_node**, uint64*); };
#define KB_READ 1
#define KB_GET_KEYBOARD_ID 2
#define KB_SET_LEDS 3
#define KB_SET_KEY_REPEAT_RATE 5
#define KB_GET_KEY_REPEAT_RATE 6
#define KB_SET_KEY_REPEAT_DELAY 7
#define KB_GET_KEY_REPEAT_DELAY 8
#define KB_SET_CONTROL_ALT_DEL_TIMEOUT 9
#define KB_CANCEL_CONTROL_ALT_DEL 11
#define KB_DELAY_CONTROL_ALT_DEL 12
#define MS_READ 100
#define MS_NUM_EVENTS 101
#define MS_SET_CLICKSPEED 109
typedef struct { bigtime_t timestamp; uint32 keycode; bool is_keydown; } raw_key_info;
typedef struct { int32 cookie; uint32 buttons; int32 xdelta; int32 ydelta; int32 clicks; int32 modifiers; bigtime_t timestamp; int32 wheel_ydelta; int32 wheel_xdelta; } mouse_movement;
#define B_DEVICE_OP_CODES_END 9999
#define B_LOW_PRIORITY 5
#define min_c(a, b) ((a) < (b) ? (a) : (b))
static inline size_t strlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
