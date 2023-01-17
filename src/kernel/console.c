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
    Semaphore can_read_sem;
} input;
#define C(x)      ((x) - '@')  // Control-x
#define BACKSPACE 127

define_rest_init(console){
    init_spinlock(&(input.lock));
    init_sem(&(input.can_read_sem), 0);
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
    isize target = n;
    isize r = 0;
    inodes.unlock(ip);
    _acquire_spinlock(&(input.lock));
    while(n > 0){
        if(input.r == input.w){
            _release_spinlock(&(input.lock));
            if(_wait_sem(&(input.can_read_sem), true) == 0){
                return -1;
            }
            _acquire_spinlock(&(input.lock));
        }
        input.r = (input.r+1) % INPUT_BUF;
        char c = input.buf[input.r];
        if(c == C('D')){
            // Save ^D for next time, to make sure
            // caller gets a 0-byte result.
            if (n < target) {
                input.r--;
            }
            break;
        }
        *(dst++) = c;
        r++;
        n--;
        if(c == '\n'){
            break;
        }
    }
    _release_spinlock(&(input.lock));
    inodes.lock(ip);
    return r;
}

void console_intr(char (*getc)()) {
    // TODO
    char c;
    _acquire_spinlock(&(input.lock));
    while((c = getc()) != 0xff){
        //printk("^^%d^^",thisproc()->pid);
        if(c == '\r'){
            c = '\n';
        }
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
            post_sem(&(input.can_read_sem));
        }
        else if(c == C('C')){
            uart_put_char('^');
            uart_put_char('C');
            printk("%d\n",thisproc()->pid);
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
                post_sem(&(input.can_read_sem));
            }
        }
    }
    _release_spinlock(&(input.lock));
}
