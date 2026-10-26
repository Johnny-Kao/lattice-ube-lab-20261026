#include <openssl/rand.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <csignal>
#include <thread>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
extern "C" uint64_t CRYPTO_get_fork_ube_generation(void);
int main(int argc,char**argv){
  int rounds=argc>1?atoi(argv[1]):12;
  unsigned char x[32];
  if(RAND_bytes(x,sizeof(x))!=1)return 2;
  uint64_t parent=CRYPTO_get_fork_ube_generation();
  if(parent==0)return 3;
  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;
  for(int i=0;i<4;++i)workers.emplace_back([&]{
    unsigned char buf[32];
    while(!stop.load(std::memory_order_relaxed)){
      if(RAND_bytes(buf,sizeof(buf))!=1)stop.store(true);
      (void)CRYPTO_get_fork_ube_generation();
    }
  });
  bool ok=true;
  for(int i=0;i<rounds;++i){
    int fd[2];if(pipe(fd)!=0){ok=false;break;}
    pid_t pid=fork();
    if(pid<0){perror("fork");ok=false;close(fd[0]);close(fd[1]);break;}
    if(pid==0){
      close(fd[0]);
      const char a='A';(void)write(fd[1],&a,1);
      uint64_t g=CRYPTO_get_fork_ube_generation();
      const char b='B';(void)write(fd[1],&b,1);
      int good=(g==parent+1)&&(RAND_bytes(x,sizeof(x))==1);
      const char c='C';(void)write(fd[1],&c,1);
      _exit(good?0:7);
    }
    close(fd[1]);char steps[8]={0};int status=0;bool ended=false;
    for(int k=0;k<200;++k){
      pid_t w=waitpid(pid,&status,WNOHANG);
      if(w==pid){ended=true;break;}
      if(w<0 && errno!=EINTR){break;}
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if(!ended){kill(pid,SIGKILL);waitpid(pid,&status,0);}
    ssize_t n=read(fd[0],steps,sizeof(steps)-1);close(fd[0]);
    bool pass=ended && WIFEXITED(status) && WEXITSTATUS(status)==0;
    printf("round=%d ended=%d stages=%s status=%d pass=%d\n",i,ended,n>0?steps:"none",status,pass);
    fflush(stdout);
    if(!pass){ok=false;break;}
  }
  stop.store(true);
  for(auto &t:workers)t.join();
  return ok?0:1;
}
