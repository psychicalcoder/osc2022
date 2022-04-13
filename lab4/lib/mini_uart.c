#include "mini_uart.h"
#include "stdio.h"

#define GPFSEL1 ((volatile unsigned int *)(0x3F200004))
#define GPPUD ((volatile unsigned int *)(0x3F200094))
#define GPPUDCLK0 ((volatile unsigned int *)(0x3F200098))

#define ENABLE_IRQs1 ((volatile unsigned int *)(0x3F00B210))

void mini_uart_init() {
  register volatile unsigned int rsel = *GPFSEL1;
  register volatile unsigned int rclk0;
  
  rsel &= 0xFFFC0FFF;
  rsel |= ((2<<12)|(2<<15));
  *GPFSEL1 = rsel;
  *GPPUD = 0;
  
  int waitcycles = 150;
  while (waitcycles--)  asm volatile("nop");
  rclk0 = ((1<<14)|(1<<15));
  *GPPUDCLK0 = rclk0;
  waitcycles = 150;
  while (waitcycles--) asm volatile("nop");
    
  *AUX_ENABLE |= 1;
  *AUX_MU_CNTL = 0;
  // *AUX_MU_IER = 0;
  // *AUX_MU_IER = 1; // enable receive interrupt (bit 1 is transmit interrupt)
  *AUX_MU_IER = 0;
  *AUX_MU_LCR = 3;
  *AUX_MU_MCR = 0;
  *AUX_MU_BAUD = 270;
  *AUX_MU_IIR = 6;
  *AUX_MU_CNTL = 3;

  // enable uart irq
  register volatile unsigned int eirqs1 = 1 << 29;
  *ENABLE_IRQs1 = eirqs1;
}

void mini_uart_send(char c) {
  while (1) {
    if ((*AUX_MU_LSR)&0x20) break;
  }
  *AUX_MU_IO = c;
}

char mini_uart_recv() {
  while (1) {
    if ((*AUX_MU_LSR)&0x01) break;
  }
  return (*AUX_MU_IO)&0xFF;
}


void print_char(const char c) {
  if (c == '\n') mini_uart_send('\r');
  mini_uart_send(c);
}

void print(const char *str) {
  while (*str) {
    print_char(*str++);
  }
}

void print_num(int num) {
  if (num == 0) {
    print_char('0');
    return;
  }
  if (num < 0) {
    print_char('-');
    num = -num;
  }
  char buf[10];
  int len = 0;
  while (num > 0) {
    buf[len++] = (char)(num%10)+'0';
    num /= 10;
  }
  for (int i = len-1; i >= 0; i--) {
    print_char(buf[i]);
  }
}

void print_hex(unsigned int num) {
  print("0x");
  int h = 28;
  while (h >= 0) {
    char ch = (num >> h) & 0xF;
    if (ch >= 10) ch += 'A' - 10;
    else ch += '0';
    print_char(ch);
    h -= 4;
  }
}

void print_hexl(unsigned long num) {
  print("0x");
  int h = 60;
  while (h >= 0) {
    char ch = (num >> h) & 0xF;
    if (ch >= 10) ch += 'A' - 10;
    else ch += '0';
    print_char(ch);
    h -= 4;
  }
}



int read(char *buf, int len) {
  char c;
  int i;
  for (i = 0; i < len; i++) {
    c = mini_uart_recv();
    if (c == 127) { i--; continue; }
    print_char(c);
    // print_num((int)c);
    if (c == '\r') {
      c = '\n';
      print_char('\n');
      break;
    }
    buf[i] = c;
  }
  buf[i] = '\0';
  return i;
}


unsigned int vsprintf(char *dst, char* fmt, __builtin_va_list args)
{
    long int arg;
    int len, sign, i;
    char *p, *orig=dst, tmpstr[19];

    // failsafes
    if(dst==(void*)0 || fmt==(void*)0) {
        return 0;
    }

    // main loop
    arg = 0;
    while(*fmt) {
        // argument access
        if(*fmt=='%') {
            fmt++;
            // literal %
            if(*fmt=='%') {
                goto put;
            }
            len=0;
            // size modifier
            while(*fmt>='0' && *fmt<='9') {
                len *= 10;
                len += *fmt-'0';
                fmt++;
            }
            // skip long modifier
            if(*fmt=='l') {
                fmt++;
            }
            // character
            if(*fmt=='c') {
                arg = __builtin_va_arg(args, int);
                *dst++ = (char)arg;
                fmt++;
                continue;
            } else
            // decimal number
            if(*fmt=='d') {
                arg = __builtin_va_arg(args, int);
                // check input
                sign=0;
                if((int)arg<0) {
                    arg*=-1;
                    sign++;
                }
                if(arg>99999999999999999L) {
                    arg=99999999999999999L;
                }
                // convert to string
                i=18;
                tmpstr[i]=0;
                do {
                    tmpstr[--i]='0'+(arg%10);
                    arg/=10;
                } while(arg!=0 && i>0);
                if(sign) {
                    tmpstr[--i]='-';
                }
                // padding, only space
                if(len>0 && len<18) {
                    while(i>18-len) {
                        tmpstr[--i]=' ';
                    }
                }
                p=&tmpstr[i];
                goto copystring;
            } else
            // hex number
            if(*fmt=='x') {
                arg = __builtin_va_arg(args, long int);
                // convert to string
                i=16;
                tmpstr[i]=0;
                do {
                    char n=arg & 0xf;
                    // 0-9 => '0'-'9', 10-15 => 'A'-'F'
                    tmpstr[--i]=n+(n>9?0x37:0x30);
                    arg>>=4;
                } while(arg!=0 && i>0);
                // padding, only leading zeros
                if(len>0 && len<=16) {
                    while(i>16-len) {
                        tmpstr[--i]='0';
                    }
                }
                p=&tmpstr[i];
                goto copystring;
            } else
            // string
            if(*fmt=='s') {
                p = __builtin_va_arg(args, char*);
copystring:     if(p==(void*)0) {
                    p="(null)";
                }
                while(*p) {
                    *dst++ = *p++;
                }
            }
        } else {
put:        *dst++ = *fmt;
        }
        fmt++;
    }
    *dst=0;
    // number of bytes written
    return dst-orig;
}

void printf(char *fmt, ...) {
    char temp[128];
    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vsprintf(temp,fmt,args);
    print(temp);
}