
#include <linux/kernel.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/mman.h>  
#include <sys/errno.h>
#include <pthread.h>

#include "stdint.h"
#include "stdio.h"
#include "time.h"
#include "stdlib.h"

// c++ stl multithread random number generator
#include <random>

/**
 * Warning : the tradition srand() is a sequential generator. 
 * 	The multiple threads will be bound at the random number generation.
 */



#define ONE_MB    1048576UL				// 1024 x 2014 bytes
#define ONE_GB    1073741824UL   	// 1024 x 1024 x 1024 bytes

#define PAGE_SIZE  4096
#define PAGE_SHIFT 12

//typedef enum {true, false} bool;

extern errno;

//#define ARRAY_BYTE_SIZE 0xc0000000UL

// #define ARRAY_START_ADDR	0x400000000000UL - 3*ONE_GB	- ONE_MB
#define ARRAY_START_ADDR	1*ONE_GB 
// #define ARRAY_BYTE_SIZE   15*ONE_GB  // 
#define ARRAY_BYTE_SIZE   64*ONE_MB  // 

int online_cores = 8;

struct thread_args{
	size_t	thread_id; // thread index, start from 0
	char* 	user_buf;
};


struct payload{
	unsigned long  data1; // 32 bytes -- 8 bytes actual
	unsigned long  data2;
	unsigned long  data3;
	unsigned long  data4;
	payload(){
		data1 = 0;
		data2 = 0;
		data3 = 0;
		data4 = 0;
	}
	payload(unsigned long d1, unsigned long d2, unsigned long d3, unsigned long d4){
		data1 = d1;
		data2 = d2;
		data3 = d3;
		data4 = d4;
	}
	unsigned long get_data(int index){
		switch(index){
			case 0:
				return data1;
			case 1:
				return data2;
			case 2:
				return data3;
			case 3:
				return data4;
			default:
				return 0;
		}
	}
};

struct objv0{ // An implementation of object header -- size: 40, 
	unsigned long* forwardTable; // 8 bytes
    unsigned long* current_ptr; // 8 bytes
    unsigned long  access_count; // 8 bytes
    unsigned long  epochID;  // 8 bytes
    payload data;
    objv0(): forwardTable(NULL), current_ptr(NULL), access_count(0), epochID(0){
        data = payload();
    }
    ~objv0(){
    }
    
    void assign_data(payload p){
        data = p;
    }

    payload get_data(){
        return data;
    }

};


/**
 * Reserve memory at fixed address 
 */
static char* reserve_anon_memory(char* requested_addr, unsigned long bytes, bool fixed) {
	char * addr;
	int flags;

	flags = MAP_PRIVATE | MAP_NORESERVE | MAP_ANONYMOUS;   
	if (fixed == true) {
			printf("Request fixed addr 0x%lx ", (unsigned long)requested_addr);

		flags |= MAP_FIXED;
	}

	// Map reserved/uncommitted pages PROT_NONE so we fail early if we
	// touch an uncommitted page. Otherwise, the read/write might
	// succeed if we have enough swap space to back the physical page.
	addr = (char*)mmap(requested_addr, bytes, PROT_NONE,
											 flags, -1, 0);

	return addr == MAP_FAILED ? NULL : addr;
}



/**
 * Commit memory at reserved memory range.
 *  
 */
char* commit_anon_memory(char* start_addr, unsigned long size, bool exec) {
	int prot = (exec == true) ? PROT_READ|PROT_WRITE|PROT_EXEC : PROT_READ|PROT_WRITE;
	unsigned long res = (unsigned long)mmap(start_addr, size, prot,
																		 MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);   // MAP_FIXED will override the old mapping
	
	// commit memory successfully.
	if (res == (unsigned long) MAP_FAILED) {

		// print errno here.
		return NULL;
	}

	return start_addr;
}



void scan_array_sequential_overleap(){

	FILE *filePointer; 
	filePointer = fopen("array_swap_output.txt", "w");

	payload data123 = payload(123,123,123,123);
	payload data456 = payload(456,456,456,456);
	payload data789 = payload(789,789,789,789);

    char* user_buff;
	size_t array_slice = ARRAY_BYTE_SIZE/sizeof(struct objv0); // the entire array
	size_t array_start = 0; 	// scan from start
	size_t i=0, sum=0, sum0 = 0;

	int ELEMENT_PER_PAGE = PAGE_SIZE / sizeof(struct objv0); // whole page
	int NUMBER_ALLOCATION = 1; //=1: interleaving; =ELEMENT_PER_PAGE: whole page
	printf("ELEMENT_PER_PAGE = %d\n", ELEMENT_PER_PAGE);

	int type = 0x1;
	unsigned long request_addr 	= ARRAY_START_ADDR; // start of RDMA meta space, 1GB not exceed the swap partitio size.
	unsigned long size  		= ARRAY_BYTE_SIZE;	// 512MB, for unsigned long, length is 0x4,000,000
	pthread_t threads[online_cores];
	struct thread_args args[online_cores]; // pass value by reference, so keep a copy for each thread
	int ret =0;
	// srand(time(NULL));		// generate a random interger
	
	srand(456); // fixed seed, for reproducibility

	//online_cores = sysconf(_SC_NPROCESSORS_ONLN);

	// 1) reserve space by mmap
	user_buff = reserve_anon_memory((char*)request_addr, size, true );
	if(user_buff == NULL){
		printf("Reserve user_buffer, 0x%lx failed. \n", (unsigned long)request_addr);
	}else{
		printf("Reserve user_buffer: 0x%lx, bytes_len: 0x%lx \n",(unsigned long)user_buff, size);
	}

	// 2) commit the space
	user_buff = commit_anon_memory((char*)request_addr, size, false);
	if(user_buff == NULL){
		printf("Commit user_buffer, 0x%lx failed. \n", (unsigned long)request_addr);
	}else{
		printf("Commit user_buffer: 0x%lx, bytes_len: 0x%lx \n",(unsigned long)user_buff, size);
	}


    // user_buff = (char *) malloc(ARRAY_BYTE_SIZE);
    // if (user_buff == NULL){
    //     printf("Dynamic malloc %lu bytes failed. \n", size);
    //     exit(0);
    // } else {
    //     printf("Dynamic malloc %lu bytes succeed. \n", size);
    // }

	printf("Writing %lu element to %lu bytes array \n", array_slice, size);
	objv0 * buf_ul_ptr = (objv0*)user_buff;
	// int tmp;
	printf("Start \n");
	// getchar();
	// scanf("%d", &tmp);
	printf("Stage 1 \n");
	
	for( i = array_start  ; i < array_start + array_slice/2  ; i++ ){
		    int cur_assign = rand() % 3;
			if ( cur_assign == 1) {
				buf_ul_ptr[i] = objv0();  // the max value. // 1C8 in HEX
				buf_ul_ptr[i].data = data456;
				sum0 += 456*4;
			} else if (cur_assign == 2) {
				buf_ul_ptr[i] = objv0();  
				buf_ul_ptr[i].data = data789;
				sum0 += 789*4;
			} else if (cur_assign == 0) { // cur_assign == 0
				buf_ul_ptr[i] = objv0();  
				buf_ul_ptr[i].data = data123;
				sum0 += 123*4;
			} else {
				printf("Error: cur_assign = %d \n", cur_assign);
			}
		
	}
	// getchar();
	// scanf("%d", &tmp);
	printf("Stage 2 \n");

	for( i = array_start+array_slice/2  ; i < array_start + array_slice  ; i++ ){
		// buf_ul_ptr[i] = 789;  // the max value. // 315 in HEX
		// sum0 += 789;
			int cur_assign = rand() % 3;
			if ( cur_assign == 1) {
				buf_ul_ptr[i] = objv0();  // the max value. // 1C8 in HEX
				buf_ul_ptr[i].data = data456;
				sum0 += 456*4;
			} else if (cur_assign == 2) {
				buf_ul_ptr[i] = objv0();  
				buf_ul_ptr[i].data = data789;
				sum0 += 789*4;
			} else  if (cur_assign == 0) { // cur_assign == 0
				buf_ul_ptr[i] = objv0();  
				buf_ul_ptr[i].data = data123;
				sum0 += 123*4;
			} else {
				printf("Error: cur_assign = %d \n", cur_assign);
			}
	}
	// Swap 500HEAD - mem 500 TAIL
	// getchar();
	// scanf("%d", &tmp);
	printf("sum0= %lu\n",sum0);

	srand(456); // Reset seed

	printf("Stage 3 \n");
	long err1 = 0, err2 = 0;
	int pr_1 = 0, pr_2 = 0;
	//long sum = 0;
	fprintf(filePointer, "---------------- Stage III ----------------\n");
	int cur_page = -1;

	for( i = array_start  ; i < array_start + array_slice/2  ; i++ ){
		payload p = buf_ul_ptr[i].get_data();
		int hot_cold = rand() % 3;
		for (int j = 0; j < 4; j++) {
			sum += p.get_data(j);
			// sum += p.get_data(j);  // the max value.
			if (cur_page != i/ELEMENT_PER_PAGE){
				cur_page = i/ELEMENT_PER_PAGE;
				fprintf(filePointer, "\n----- Page: %d\n", cur_page);
			}
			if (p.get_data(j)==456){
				fprintf(filePointer, "1");
			}
			else if (p.get_data(j)==789){
				fprintf(filePointer, "2");
			} else if (p.get_data(j)==123){ 
				fprintf(filePointer, "0"); 
			} else {
				fprintf(filePointer, "X");
			}
			// fprintf(filePointer, "%lu\n", p.get_data(j));


			// if( p.get_data(j) == 123){
			// 	printf("Array Modified. \n");
			// }
			if ( hot_cold == 1) {
				if (p.get_data(j) != 456){
					if(pr_1 < 2){
						printf("PTEPs Swapped in Stage 3 from 456 to %lu. \n", p.get_data(j));
						
						pr_1++;
					}
					// ++cnt2;
				}
			}else if (hot_cold == 2) {
				if (p.get_data(j) != 789){
					if(pr_1 < 2){
						printf("PTEPs Swapped in Stage 3 from 789 to %lu. \n", p.get_data(j));
						pr_1++;
					}
					// ++cnt;
				}
			} else { // hot_cold == 0
				if (p.get_data(j) != 123){
					if(pr_1 < 2){
						printf("PTEPs Swapped in Stage 3 from 123 to %lu. \n", p.get_data(j));
						pr_1++;
					}
					// ++cnt2;
				}
			}

		
			
			if (p.get_data(j)!=456 && p.get_data(j)!=789 && p.get_data(j)!=123) {
				printf("Error page: %lu \n", p.get_data(j));
				++err1;
			}
		}
	}
	// getchar();
	// scanf("%d", &tmp);
	printf("Stage 4 \n");
	fprintf(filePointer, "\n---------------- Stage IV ----------------\n");
	cur_page = -1;
	for( i = array_start+array_slice/2  ; i < array_start + array_slice  ; i++ ){
		payload p = buf_ul_ptr[i].get_data();
		int hot_cold = rand() % 3;
        for (int j = 0; j < 4; j++) {
            sum += p.get_data(j);

			if (cur_page != i/ELEMENT_PER_PAGE){
				cur_page = i/ELEMENT_PER_PAGE;
				fprintf(filePointer, "\n----- Page: %d\n", cur_page);
			}
			// fprintf(filePointer, "%lu\n", p.get_data(j));
			if (p.get_data(j)==456){
				fprintf(filePointer, "1");
			}
			else if (p.get_data(j)==789){
				fprintf(filePointer, "2");
			} else if (p.get_data(j)==123){
				fprintf(filePointer, "0");
			} else {
				fprintf(filePointer, "X");
			}


			// sum += p.get_data(j);  // the max value.
			// if( p.get_data(j) == 123){
			// 	printf("Array Modified. \n");
			// }


			if ( hot_cold == 1) {
				if (p.get_data(j) != 456){
					if(pr_2 < 2){
						printf("PTEPs Swapped in Stage 4 from 456 to %lu. \n", p.get_data(j));
						pr_2++;
					} 
					// ++cnt2;
				}
			}else if (hot_cold == 2){
				if (p.get_data(j) != 789){
					if(pr_2 < 2){
						printf("PTEPs Swapped in Stage 4 from 789 to %lu. \n", p.get_data(j));
						pr_2++;
					}
					// ++cnt;
				}
			} else if (hot_cold == 0){
				if (p.get_data(j) != 123){
					if(pr_2 < 2){
						printf("PTEPs Swapped in Stage 4 from 123 to %lu. \n", p.get_data(j));
						pr_2++;
					}
					// ++cnt;
				}
			}

			
			
			if (p.get_data(j)!=456 && p.get_data(j)!=789 && p.get_data(j)!=123) {
				printf("Error page: %lu \n", p.get_data(j));
				++err1;
			}
		}
	}
	printf("sum = %lu\n",sum);
	// printf("cnt 456 = %lu, cnt 789 = %lu\n",cnt,cnt2);
	printf("err 456 = %lu, err 789 = %lu\n",err1,err2);
	if (sum0 == sum){
		printf("Sum are the SAME.\n");
	} else {
		printf("Sum are the DIFFERENT.\n");
		printf("Diff = %lld.\n", (long long)((long long)sum0 - (long long)sum));
	}

// Swap 500HEAD - mem 500 TAIL
	printf("Finished. Start to access one page.\n");

	fclose(filePointer);

	// getchar();
	// int tmp;
	// int tmp2;
	// scanf("%d", &tmp2);
	// printf("Value = %d\n",tmp2);
	// scanf("start: %d\n", &tmp);
	// pause();
	// while(1);
	// for( i = array_start  ; i < array_start + 1000  ; i++ ){
		
	// 	printf("The first element on the 2nd page is: %lu \n", p.get_data(j) );
	// }
    // i = array_start + (PAGE_SIZE / sizeof(unsigned long));
	// printf("Reading 1st element of the array on the 2nd page \n");

	// printf("The first element on the 2nd page is: %lu \n", p.get_data(j) );
	// scanf("%d", &tmp2);

	// printf("Next....Start to update page...\n");
	// // getchar();
	// for( i = array_start  ; i < array_start + (PAGE_SIZE / sizeof(unsigned long))*10  ; i++ ){
	// 	p.get_data(j) += 1;
	// }
	// scanf("%d", &tmp2);
	printf("Finished....\n");
	// getchar();
}


// g++ swap_mmap_multi_thread.cpp -static -o swap_cgroup
// ./swap_cgroup
int main(){

    scan_array_sequential_overleap(); 

	return 0; //useless ?
}
