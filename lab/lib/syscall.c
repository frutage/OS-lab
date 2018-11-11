#include "lib.h"
#include "types.h"
#include <stdarg.h>
/*
 * io lib here
 * 库函数写在这
 */
int32_t syscall(int num, uint32_t a1,uint32_t a2,
		uint32_t a3, uint32_t a4, uint32_t a5)
{
        uint32_t ret = 0;
	/* 内嵌汇编 保存 num, a1, a2, a3, a4, a5 至通用寄存器*/

	asm volatile("int $0x80":"=a"(ret):"a"(num),"b"(a1),"c"(a2),"d"(a3),"D"(a4),"S"(a5));

	return ret;
}

int sem_init(sem_t *sem, uint32_t value){
	int ret = 0;
	int num = 8;
	int sem_num = 0;
	asm volatile("int $0x80":"=a"(sem_num):"a"(num),"d"(value));
	*sem = sem_num;
	return ret;	
}

int sem_post(sem_t *sem){
	int ret = 0;
	int num = 9;
	asm volatile("int $0x80":"=a"(ret):"a"(num),"d"(*sem));
	return ret;
}

int sem_wait(sem_t *sem){
	int ret = 0;
	int num = 10;
	asm volatile("int $0x80":"=a"(ret):"a"(num),"d"(*sem));
	return ret;
}

int sem_destroy(sem_t *sem){
	int ret = 0;
	int num = 11;
	asm volatile("int $0x80":"=a"(ret):"a"(num),"d"(*sem));
	return ret;
}

int fork() {
	int ret = -1; 
	int num = 2;
	asm volatile("int $0x80":"=a"(ret):"a"(num));
	return ret;
}

int sleep(int time) {
	int ret = 0;
	int num = 7;
	asm volatile("int $0x80":"=a"(ret):"a"(num),"d"(time));
	return ret;
}

int exit() {
	int ret = 0;
	int num = 1;
	asm volatile("int $0x80":"=a"(ret):"a"(num));
	return ret;
}

char res[200];
void printf(const char *format,...){
	va_list ap;
	int pos = 0;
	va_start(ap, format);//mov address of format to ap
	res[0] = '\0';
	while(*format != '\0') {
		if(*format != '%')
		{
			res[pos++] = *format;
			format ++;
		}
		else {
			format ++;
			switch(*format)	{
			case 'c': {
				char ch = va_arg(ap, int);
				res[pos++] = ch;
			} break;
		       	case 's': {
				char *st = va_arg(ap, char *);
				int i = 0;
				while(st[i] != '\0') {
					res[pos++] = st[i++];
				}
			} break;
			case 'd': {
				char inv[30];
				int ival = va_arg(ap, int);
			//	itoa(ival, inv, 10);
				int flag = 0;
				int cnt = 0;
				if(ival == -2147483648) 
				{
					res[pos++] = '-';
					res[pos++] = '2';
					res[pos++] = '1';
					res[pos++] = '4';
					res[pos++] = '7';
					res[pos++] = '4';
					res[pos++] = '8';
					res[pos++] = '3';
					res[pos++] = '6';
					res[pos++] = '4';
					res[pos++] = '8';
				}
				else {

				if(ival < 0) {
					flag = 1;
					ival = -ival;
				}

				inv[cnt] = (ival % 10) + '0';
				int val = ival / 10;
				while(val != 0)
				{
					cnt ++;
					inv[cnt] = (val % 10) + '0';
					val /= 10;
				}
				if(flag == 1){
					cnt ++;
					inv[cnt] = '-';
				}
				inv[cnt + 1] = '\0';
				for(int j = 0; j <= cnt; cnt--,j++)
				{
					char t = inv[j];	
					inv[j] = inv[cnt];
					inv[cnt] = t;
				}

				int i = 0;
				while(inv[i] != '\0') {
					res[pos++] = inv[i++];
				}
		
				}
			} break;
			case 'x': {
				char hexv[30];
				unsigned int ival = va_arg(ap, int);
			//	itoa(ival, hexv, 16);
				int cnt = 0;
				if((ival & 0xf) <= 9)
					hexv[cnt] = (ival & 0xf) + '0';
				else
					hexv[cnt] = (ival & 0xf) - 10 + 'a';
                                unsigned int val = (ival >>  4);
                                while(val != 0)
                                {
                                        cnt ++;
					if((val & 0xf) <= 9)
						hexv[cnt] = (val & 0xf) + '0';
					else
						hexv[cnt] = (val & 0xf) - 10  + 'a';
                                        val >>= 4;
                                }
				hexv[cnt + 1] = '\0';
                                for(int j = 0; j <= cnt; cnt--,j++)
                                {
                                        char t = hexv[j];
                                        hexv[j] = hexv[cnt];
                                        hexv[cnt] = t;
                                }
		
				int i = 0;
				while(hexv[i] != '\0'){
					res[pos++] = hexv[i++];
				}
			} break;
			default:break;
			}
			format ++;
		}
	}
	res[pos] = *format;					
	int len = pos + 1;
	if((uint32_t)res == 0x201760)
		syscall(4, 1, (uint32_t)res, len, 0, 0);
//	res[0] = '\0';		
}
		
