#include "kernel/types.h"
#include "kernel/stat.h" 
#include "user/user.h" 

int main() { 

	int pid= getpid();
	int fd; 

	char fname[32], lname[32], age[8], color[32], city[32], country[32];

	fd = open("userdata.txt", 513); // open for writing 
	if(fd < 0) exit(1); // create if it doesn't exist
	  
	printf("Iobound started\n");
	// for (int i = 0; i < 10; ++i)
	// {
	// 	printf("Enter your name: ");
	// 	gets(name, sizeof(name));
		
	// 	for (volatile int j = 0; j < 200000000; j++);			//so it stays long enough for one tick

	// 	write(fd, name, strlen(name));
	// 	write(fd, "\n", 1);
	// }
	
	printf("Enter your First Name: ");
    gets(fname, sizeof(fname));
    for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "First Name: ", 12);
    write(fd, fname, strlen(fname));
    write(fd, "\n", 1);

	printf("Enter your Last Name: ");
	gets(lname, sizeof(lname));
	for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "Last Name: ", 11);
	write(fd, lname, strlen(lname));
	write(fd, "\n", 1);

	printf("Enter your Age: ");
	gets(age, sizeof(age));
	for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "Age: ", 5);
	write(fd, age, strlen(age));
	write(fd, "\n", 1);

	printf("Enter your Favorite Color: ");
	gets(color, sizeof(color));
	for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "Favorite Color: ", 16);
	write(fd, color, strlen(color));
	write(fd, "\n", 1);

	printf("Enter your City: ");
	gets(city, sizeof(city));
	for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "City: ", 6);
	write(fd, city, strlen(city));
	write(fd, "\n", 1);

	printf("Enter your Country: ");
	gets(country, sizeof(country));
	for (volatile int j = 0; j < 200000000; j++) {};
	write(fd, "Country: ", 9);
	write(fd, country, strlen(country));
	write(fd, "\n", 1);

	close(fd);

	getprocinfo(pid); 

	exit(0);
}
