#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"


 int fibonacci(int n)
  {
   if (n <= 1) return n;
   return fibonacci(n - 1) + fibonacci(n - 2);
  }

int main(){ 
	int N = 50; // larger value = heavier CPU load int

	int pid=getpid();	//get the process id

	int result = fibonacci(N); 
	printf("Final Fibonacci(%d) = %d\n", N, result);
	getprocinfo(pid); 	//new syscall
	exit(0); 
}