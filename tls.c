#include "tls.h"
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
/*
 * This is a good place to define any data structures you will use in this file.
 * For example:
 *  - struct TLS: may indicate information about a thread's local storage
 *    (which thread, how much storage, where is the storage in memory)
 *  - struct page: May indicate a shareable unit of memory (we specified in
 *    homework prompt that you don't need to offer fine-grain cloning and CoW,
 *    and that page granularity is sufficient). Relevant information for sharing
 *    could be: where is the shared page's data, and how many threads are sharing it
 *  - Some kind of data structure to help find a TLS, searching by thread ID.
 *    E.g., a list of thread IDs and their related TLS structs, or a hash table.
 */
#define MAX_THREAD_COUNT 128

typedef struct thread_local_storage
{
 pthread_t tid;
 unsigned int size; /* size in bytes */
 unsigned int page_num; /* number of pages */
 struct page **pages; /* array of pointers to pages */
} TLS;

struct page {
 unsigned int address; /* start address of page */
 int ref_count; /* counter for shared pages */
};

struct tid_tls_pair
{
 pthread_t tid;
 TLS *tls;
};

static struct tid_tls_pair tid_tls_pairs[MAX_THREAD_COUNT];
/*
 * Now that data structures are defined, here's a good place to declare any
 * global variables.
 */
#define page_size getpagesize()

/*
 * With global data declared, this is a good point to start defining your
 * static helper functions.
 */
void tls_protect(struct page *p)
{
 if (mprotect((void *) (intptr_t) p->address, page_size, PROT_NONE)) {
 fprintf(stderr, "tls_protect: could not protect page\n");
 exit(1);
 }
}

void tls_unprotect(struct page *p)
{
 if (mprotect((void *) (intptr_t) p->address, page_size, PROT_READ|PROT_WRITE)) {
 fprintf(stderr, "tls_unprotect: could not unprotect page\n");
 exit(1);
 }
}
/*
 * Lastly, here is a good place to add your externally-callable functions.
 */ 
void tls_handle_page_fault(int sig, siginfo_t *si, void *context)
{
	unsigned int p_fault = ((uintptr_t) si->si_addr) & ~(page_size - 1);
	for (int i = 0; i < MAX_THREAD_COUNT; i++)
	{
		for (int j = 0; j < tid_tls_pairs[i].tls->page_num; j++)
		{
			if (tid_tls_pairs[i].tls->pages[j]->address == p_fault)
			{
				// exit current threads
				pthread_exit(0);
				return;
			}
		}
	}
	signal(SIGSEGV, SIG_DFL);
	signal(SIGBUS, SIG_DFL);
	raise(sig);
	return;

}

void tls_init()//?????
{
	struct sigaction sigact;
	// page_size = getpagesize();
	/* Handle page faults (SIGSEGV, SIGBUS) */
	sigemptyset(&sigact.sa_mask);
	/* Give context to handler */
	sigact.sa_flags = SA_SIGINFO;
	sigact.sa_sigaction = tls_handle_page_fault;
	sigaction(SIGBUS, &sigact, NULL);
	sigaction(SIGSEGV, &sigact, NULL);
}



int tls_create(unsigned int size)
{
	if (size == 0)
		return -1;
	if (tid_tls_pairs[pthread_self()].tls != NULL)
		return -1;

	TLS *myTLS;
	myTLS = (TLS*)malloc(sizeof(TLS));

	myTLS->tid = pthread_self();
	myTLS->size = size;
	myTLS->page_num = size/page_size + 1;
	myTLS->pages = (struct page **)calloc(myTLS->page_num, page_size);
	for (int i = 0; i < myTLS->page_num; i++)
	{
		myTLS->pages[i]->address = (uintptr_t) mmap(0, page_size, PROT_NONE, MAP_ANON|MAP_PRIVATE, 0, 0);
		myTLS->pages[i]->ref_count = 0;
	}
	tid_tls_pairs[pthread_self()].tls = myTLS;
	tid_tls_pairs[pthread_self()].tid = pthread_self();
	



	return 0;
}

int tls_destroy()
{
	if (tid_tls_pairs[pthread_self()].tls == NULL)
		return -1;
	for (int i = 0; i < tid_tls_pairs[pthread_self()].tls->page_num; i++)
	{
		if (tid_tls_pairs[pthread_self()].tls->pages[i]->ref_count == 0)
		{
			free(tid_tls_pairs[pthread_self()].tls->pages[i]);
		} 
		else
		{
			tid_tls_pairs[pthread_self()].tls->pages[i]->ref_count--;
			// look up
		}
	}
	free(tid_tls_pairs[pthread_self()].tls);
	// how to remove the (tid->tls) from global structure
	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer)
{
	if (tid_tls_pairs[pthread_self()].tls == NULL || offset + length > tid_tls_pairs[pthread_self()].tls->size)
		return -1;
	for (int i = 0; i < tid_tls_pairs[pthread_self()].tls->page_num; i++)
	{
		tls_unprotect(tid_tls_pairs[pthread_self()].tls->pages[i]);
	}

	for (int count = 0, idx = offset; idx < (offset + length); count++, idx++)
	{
		int pageNum = idx/page_size;
		int pageOffset = idx%page_size;
		buffer[count] = *(tid_tls_pairs[pthread_self()].tls->pages[pageNum]->address + pageOffset);
	}

	for (int i = 0; i < tid_tls_pairs[pthread_self()].tls->page_num; i++)
	{
		tls_protect(tid_tls_pairs[pthread_self()].tls->pages[i]);
	}
	return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer)
{
	if (tid_tls_pairs[pthread_self()].tls == NULL || offset + length > tid_tls_pairs[pthread_self()].tls->size)
		return -1;
	for (int i = 0; i < tid_tls_pairs[pthread_self()].tls->page_num; i++)
	{
		tls_unprotect(tid_tls_pairs[pthread_self()].tls->pages[i]);
	}

	for (int count = 0, idx = offset; idx < (offset + length); count++, idx++)
	{
		int pageNum = idx/page_size;
		int pageOffset = idx%page_size;
		if (tid_tls_pairs[pthread_self()].tls->pages[pageNum]->ref_count > 0)
		{
			tid_tls_pairs[pthread_self()].tls->pages[pageNum]->ref_count--;
			tls_protect(tid_tls_pairs[pthread_self()].tls->pages[pageNum]);
			struct page* copy = (struct page*) calloc(1, sizeof(struct page));
			copy->address = (uintptr_t)mmap(0, page_size, PROT_WRITE, MAP_ANON|MAP_PRIVATE, 0, 0);
			copy->ref_count = 0;
			// what about the data in the old page

			tid_tls_pairs[pthread_self()].tls->pages[pageNum] = copy;

		}

		*(tid_tls_pairs[pthread_self()].tls->pages[pageNum]->address + pageOffset) = buffer[count];

	}

	for (int i = 0; i < tid_tls_pairs[pthread_self()].tls->page_num; i++)
	{
		tls_protect(tid_tls_pairs[pthread_self()].tls->pages[i]);
	}
	return 0;
}

int tls_clone(pthread_t tid)
{
	if (tid_tls_pairs[pthread_self()].tls != NULL || tid_tls_pairs[tid].tls == NULL)
		return -1;
	TLS *myTLS;
	myTLS = (TLS*)malloc(sizeof(TLS));
	myTLS->tid = pthread_self();
	myTLS->size = tid_tls_pairs[tid].tls->size;
	myTLS->page_num = tid_tls_pairs[tid].tls->size/page_size + 1;

	for (int i = 0; i < myTLS->page_num; i++)
	{
		tid_tls_pairs[tid].tls->pages[i]->ref_count++;
		myTLS->pages[i]->address = tid_tls_pairs[tid].tls->pages[i]->address;
	}

	tid_tls_pairs[pthread_self()].tls = myTLS;
	tid_tls_pairs[pthread_self()].tid = pthread_self();
	return 0;
}
