// Prints the vtable a class type_info points to, at run time.
#include <stdio.h>
#include <typeinfo>
#include <OS.h>
#include <image.h>

struct Base { virtual ~Base() {} };
struct Thing : Base { int x; };

static void where(const void* address)
{
	image_info info;
	int32 cookie = 0;
	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if ((addr_t)address >= (addr_t)info.data
			&& (addr_t)address < (addr_t)info.data + info.data_size) {
			printf(" (%s data + %#lx)", info.name, (addr_t)address - (addr_t)info.data);
			return;
		}
		if ((addr_t)address >= (addr_t)info.text
			&& (addr_t)address < (addr_t)info.text + info.text_size) {
			printf(" (%s text + %#lx)", info.name, (addr_t)address - (addr_t)info.text);
			return;
		}
	}
}

int main()
{
	const std::type_info* types[] = { &typeid(Base), &typeid(Thing) };
	for (const std::type_info* t : types) {
		void** vptr = *(void***)t;
		printf("VTTEST %s: vptr %p", t->name(), vptr); where(vptr); printf("\n");
		for (int i = -2; i < 8; i++) {
			printf("VTTEST   [%d] %p", i, vptr[i]); where(vptr[i]); printf("\n");
		}
	}
	try { throw Thing(); } catch (const Base&) { printf("VTTEST caught\n"); }
	return 0;
}
