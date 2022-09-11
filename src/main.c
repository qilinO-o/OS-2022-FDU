#include <aarch64/intrinsic.h>
#include <kernel/init.h>
#include <driver/uart.h>
#include <common/string.h>

static char hello[16];

define_early_init(hello){
    strncpy(hello, "Hello world!", 16);
}

define_init(print){
    for(char *p = hello; *p; p++){
        uart_put_char(*p);
    }
}

NO_RETURN void main()
{
    extern char etext[], data[], edata[], end[];
    memset(edata, 0, end - edata);
    
    if(cpuid() == 0){
        do_early_init();
        do_init();
    }
    arch_stop_cpu();
}
