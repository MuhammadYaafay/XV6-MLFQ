#include "kernel/types.h"
#include "kernel/stat.h" 
#include "user/user.h" 

int main() { 

	int pid= getpid();

	char firstname[32], 
	lastname[32], 
	dob[16], 
	age[4], 
	city[32],
	country[32], 
	color[16]; 
	int fd; 
	fd = open("userdata.txt", 513); // open for writing 
	if(fd < 0) exit(1); // create if it doesn't exist
	  

	printf("Enter your First Name: ");
	gets(firstname, sizeof(firstname)); 
	printf("Enter your Last Name: "); 
	gets(lastname, sizeof(lastname));
	printf("Enter your Date of Birth (DD/MM/YYYY): ");
	gets(dob, sizeof(dob)); 
	printf("Enter your Age: "); 
	gets(age, sizeof(age));
	printf("Enter your City: "); 
	gets(city, sizeof(city));
	printf("Enter your Country: "); 
	gets(country, sizeof(country));
	printf("Enter your Favorite Color: ");
	gets(color, sizeof(color));

	write(fd, "User Form:\n", 11);
	write(fd, firstname, strlen(firstname));
	write(fd, " ", 1);
	write(fd, lastname, strlen(lastname));
	write(fd, "\nDOB: ", 6);
	write(fd, dob, strlen(dob)); 
	write(fd, "\nAge: ", 6); 
	write(fd, age, strlen(age));
	write(fd, "\nCity: ", 7);
	write(fd, city, strlen(city)); 
	write(fd, "\nCountry: ", 10);
	write(fd, country, strlen(country)); 
	write(fd, "\nFavorite Color: ", 17); 
	write(fd, color, strlen(color));
	write(fd, "\n\n", 2); close(fd); 
	   

	getprocinfo(pid); 

	exit(0);
}
