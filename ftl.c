// 주의사항
// 1. blockmap.h에 정의되어 있는 상수 변수를 우선적으로 사용해야 함
// 2. blockmap.h에 정의되어 있지 않을 경우 본인이 이 파일에서 만들어서 사용하면 됨
// 3. 필요한 data structure가 필요하면 이 파일에서 정의해서 쓰기 바람(blockmap.h에 추가하면 안됨)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdbool.h>
#include "blockmap.h"
#include <unistd.h>
// 필요한 경우 헤더 파일을 추가하시오.

//
// flash memory를 처음 사용할 때 필요한 초기화 작업, 예를 들면 address mapping table에 대한
// 초기화 등의 작업을 수행한다. 따라서, 첫 번째 ftl_write() 또는 ftl_read()가 호출되기 전에
// file system에 의해 반드시 먼저 호출이 되어야 한다.
//

int AddrMapTable[BLOCKS_PER_DEVICE];

void ftl_open()
{
	//
	// address mapping table 초기화 또는 복구
	// free block's pbn 초기화
	// address mapping table에서 lbn 수는 DATABLKS_PER_DEVICE 동일
	memset(AddrMapTable, -1, sizeof(AddrMapTable));
	bool chk = false;
	char pagebuf[PAGE_SIZE];
	int lbn;
	for(int i = 0; i < BLOCKS_PER_DEVICE; i++) {
		if(dd_read(i*PAGES_PER_BLOCK, pagebuf) == 1) {
			memcpy(&lbn, pagebuf+SECTOR_SIZE, 4);
			if(lbn !=  0xffffffff) {
				AddrMapTable[lbn] = i;
			}
			else {//freeblock 생성
				if(chk) continue;
				AddrMapTable[DATABLKS_PER_DEVICE] = i;
				chk = true;
			}
		}
		else {
			fprintf(stderr, "flashmemory file read error\n");
			exit(1);
		}
	}

	return;
}

//
// 이 함수를 호출하는 쪽(file system)에서 이미 sectorbuf가 가리키는 곳에 512B의 메모리가 할당되어 있어야 함
// (즉, 이 함수에서 메모리를 할당 받으면 안됨)
//
void ftl_read(int lsn, char *sectorbuf)
{
	int lbn = lsn/PAGES_PER_BLOCK;
	int ppn = lsn%PAGES_PER_BLOCK;
	int pbn = AddrMapTable[lbn];
	char pagebuf[PAGE_SIZE];
	if(dd_read(pbn*PAGES_PER_BLOCK + ppn, pagebuf) == 1) {
		memcpy(sectorbuf, pagebuf, SECTOR_SIZE);
	}
	else {
		fprintf(stderr, "flashmemory read error\n");
	}
	return;
}

//
// 이 함수를 호출하는 쪽(file system)에서 이미 sectorbuf가 가리키는 곳에 512B의 메모리가 할당되어 있어야 함
// (즉, 이 함수에서 메모리를 할당 받으면 안됨)
//
void ftl_write(int lsn, char *sectorbuf)
{
	int lbn = lsn / PAGES_PER_BLOCK;
	int ppn = lsn % PAGES_PER_BLOCK;
	int n;
	char inpage[PAGE_SIZE];
	char pagebuf[PAGE_SIZE];
	if(AddrMapTable[lbn] == -1) {
		for(int i = 0; i < BLOCKS_PER_DEVICE; i++) {
			if(i == AddrMapTable[DATABLKS_PER_DEVICE]) continue;
			if(dd_read(i*PAGES_PER_BLOCK, pagebuf) == 1) {
				memcpy(&n, pagebuf+SECTOR_SIZE, 4);
				if(n == 0xffffffff) {
					AddrMapTable[lbn] = i;
					memset(inpage, 0xFF, sizeof(inpage));
					memcpy(inpage+SECTOR_SIZE, &lbn, 4);
					//memcpy(inpage+SECTOR_SIZE + 4, &lsn, 4);
					if(dd_write(i*PAGES_PER_BLOCK, inpage) == 1) {
						memset(inpage, 0xFF, sizeof(inpage));
						memcpy(inpage, sectorbuf, SECTOR_SIZE);
						memcpy(inpage +SECTOR_SIZE, &lbn, sizeof(lbn));
						memcpy(inpage + SECTOR_SIZE + 4, &lsn, sizeof(lsn));
						if(dd_write(i*PAGES_PER_BLOCK + ppn, inpage) == -1) {
							fprintf(stderr, "file write error\n");
							exit(1);
						}
						break;
					}
					else {
						fprintf(stderr, "file write error\n");
						exit(1);
					}
				}
			}
			else {
				fprintf(stderr, "file read error\n");
				exit(1);
			}
		}
	}
	else {
		int pbn = AddrMapTable[lbn];
		int tlbn, tlsn;
		int freeblk = AddrMapTable[DATABLKS_PER_DEVICE];
		if(dd_read(pbn*PAGES_PER_BLOCK + ppn, pagebuf) == 1) {
			memcpy(&tlbn, pagebuf + SECTOR_SIZE, sizeof(tlbn));
			memcpy(&tlsn, pagebuf+SECTOR_SIZE + 4, sizeof(tlsn));
			if(tlsn == 0xffffffff) {
				memset(inpage, 0xFF, sizeof(inpage));
				memcpy(inpage, sectorbuf, SECTOR_SIZE);
				memcpy(inpage + SECTOR_SIZE, &lbn, sizeof(lbn));
				memcpy(inpage +SECTOR_SIZE + 4, &lsn, sizeof(lsn));
				dd_write(pbn*PAGES_PER_BLOCK + ppn, inpage);
			}
			else {//이미 페이지가 할당된 경우
				for(int i = 0; i < PAGES_PER_BLOCK; i++) {
					if(i != ppn) {//overwrite하려는 페이지가 아닌 경우
						if(dd_read(pbn*PAGES_PER_BLOCK+i, pagebuf) == 1) {
							if(dd_write(freeblk*PAGES_PER_BLOCK + i, pagebuf) == -1) {
								fprintf(stderr, "flashmemory write error\n");
								exit(1);
							}
						}
						else {
							fprintf(stderr, "flashmemory read error\n");
							exit(1);
						}
					}
					else {
						memset(pagebuf, 0xff, sizeof(pagebuf));
						memcpy(pagebuf, sectorbuf, SECTOR_SIZE);
						memcpy(pagebuf+SECTOR_SIZE, &lbn, sizeof(lbn));
						memcpy(pagebuf+SECTOR_SIZE+4, &lsn, sizeof(lsn));
						if(dd_write(freeblk*PAGES_PER_BLOCK	 + ppn, pagebuf) == -1) {
							fprintf(stderr, "flashmemory write error\n");
							exit(1);
						}
					}
				}
			
				AddrMapTable[lbn] = freeblk;
				dd_erase(pbn);
				AddrMapTable[DATABLKS_PER_DEVICE] = pbn;
			}
		}
		else {
			fprintf(stderr, "file read error\n");
			exit(1);
		}
	}

	return;
}

void ftl_print()
{
	printf("lbn pbn\n");
	for(int i = 0; i < DATABLKS_PER_DEVICE; i++) {
		printf("%d %d\n", i, AddrMapTable[i]);
	}
	printf("free block's pbn=%d\n", AddrMapTable[DATABLKS_PER_DEVICE]);
	return;
}

