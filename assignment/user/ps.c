#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char* argv[]){
    printf("pid,state,name\n");
    if(argc < 2){ // print all states of all processes.
        ps(0);
    }
    else{ // print all states of processes whose pid is argv[i]
      for(int i=1; i<argc; i++){
        ps(atoi(argv[i]));
      }
    }
    return 0;
}