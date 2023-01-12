#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#include <driver/interrupt.h>

#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
    SpinLock lock;
} input;
#define C(x)      ((x) - '@')  // Control-x
#define BACKSPACE '\b'

define_rest_init(console){
    init_spinlock(&(input.lock));
    set_interrupt_handler(IRQ_AUX, console_intr);
}

isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);
    _acquire_spinlock(&(input.lock));
    for(int i=0;i<n;++i){
        uart_put_char(buf[i] & 0xff);
    }
    _release_spinlock(&(input.lock));
    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {
    // TODO
    isize r = 0;
    inodes.unlock(ip);
    _acquire_spinlock(&(input.lock));
    while(n > 0){
        input.r = (input.r+1) % INPUT_BUF;
        char c = input.buf[input.r];
        if(c == C('D')){
            break;
        }
        *(dst++) = c;
        r++;
        n--;
    }
    _release_spinlock(&(input.lock));
    inodes.lock(ip);
    return r;
}

void console_intr(char (*getc)()) {
    // TODO
    char c;
    _acquire_spinlock(&(input.lock));
    while(uart_valid_char(c = getc())){
        if(c == BACKSPACE){
            if(input.e != input.w){
                input.e = (input.e-1) % INPUT_BUF;
                uart_put_char('\b');
                uart_put_char(' ');
                uart_put_char('\b');
            }
        }
        else if(c == C('U')){
            while(input.e != input.w && input.buf[(input.e-1)%INPUT_BUF] != '\n'){
                input.e = (input.e-1) % INPUT_BUF;
                uart_put_char('\b');
                uart_put_char(' ');
                uart_put_char('\b');
            }
        }
        else if(c == C('D')){
            if((input.e + 1)%INPUT_BUF == input.r){
                continue;
            }
            input.e = (input.e+1) % INPUT_BUF;
            input.buf[input.e] = c;
            uart_put_char(c);
            input.w = input.e;
        }
        else if(c == C('C')){
            uart_put_char('^');
            uart_put_char('C');
            ASSERT(kill(thisproc()->pid)!=-1);
        }
        else{ // normal char
            if((input.e + 1)%INPUT_BUF == input.r){
                continue;
            }
            input.e = (input.e+1) % INPUT_BUF;
            input.buf[input.e] = c;
            uart_put_char(c);
            if(c == '\n' || (input.e + 1)%INPUT_BUF == input.r){
                input.w = input.e;
            }
        }
    }
    _release_spinlock(&(input.lock));
}
