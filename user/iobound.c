#include "kernel/types.h"
#include "kernel/stat.h" 
#include "user/user.h" 

int main() { 

	int pid= getpid();

	char name[32];

	int fd; 
	fd = open("userdata.txt", 513); // open for writing 
	if(fd < 0) exit(1); // create if it doesn't exist
	  
	printf("Iobound started\n");
	for (int i = 0; i < 10; ++i)
	{
		printf("Enter your name: ");
		gets(name, sizeof(name));
		
		for (volatile int j = 0; j < 200000000; j++);			//so it stays long enough for one tick

		write(fd, name, strlen(name));
		write(fd, "\n", 1);
	}
	
	close(fd);

	getprocinfo(pid); 

	exit(0);
}
