#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

#define PATH_MAX 128
#define LINE_MAX 256
#define ARG_MAX 16

struct node_info { uint64_t size, entries; uint32_t type; char name[56], path[128], filesystem[16]; };
struct uptime_info { uint64_t ticks, frequency; };
struct mount_info { char path[128], filesystem[16]; };
struct block_request { const char* path; uint64_t block; void* buffer; uint64_t capacity; };

static char cwd[PATH_MAX] = "/";
static char line[LINE_MAX];
static char* argv[ARG_MAX];
static int argc;

static size_t length(const char* s) { size_t n = 0; while (s && s[n]) ++n; return n; }
static int equal(const char* a, const char* b) { while (*a && *a == *b) { ++a; ++b; } return *a == *b; }
static void copy(char* out, size_t cap, const char* in) { size_t i=0; if (!cap) return; while (in[i] && i+1<cap) { out[i]=in[i]; ++i; } out[i]=0; }
static void puts2(const char* s) { write(1, s, length(s)); }
static void putln(const char* s) { puts2(s); puts2("\n"); }
static void number(uint64_t n) { char b[24]; size_t i=sizeof(b); do { b[--i]=(char)('0'+n%10); n/=10; } while(n); write(1,b+i,sizeof(b)-i); }

static int resolve(const char* input, char* output) {
    char joined[PATH_MAX]; size_t used=0;
    if (!input || !*input) input=".";
    if (*input!='/') { copy(joined,sizeof(joined),cwd); used=length(joined); if (used>1) joined[used++]='/'; }
    for (size_t i=0; input[i] && used+1<sizeof(joined); ++i) joined[used++]=input[i];
    if (input[used ? 0 : 0]=='/' ) { used=0; for(size_t i=0;input[i]&&used+1<sizeof(joined);++i) joined[used++]=input[i]; }
    joined[used]=0; output[0]='/'; size_t out=1, i=0;
    while (joined[i]) { while(joined[i]=='/') ++i; size_t start=i; while(joined[i]&&joined[i]!='/') ++i; size_t n=i-start; if(!n) break;
        if(n==1&&joined[start]=='.') continue;
        if(n==2&&joined[start]=='.'&&joined[start+1]=='.') { if(out>1){--out;while(out>1&&output[out-1]!='/')--out;} continue; }
        if(out>1) output[out++]='/'; if(out+n>=PATH_MAX) return -1; for(size_t j=0;j<n;++j) output[out++]=joined[start+j];
    }
    output[out]=0; return 0;
}

static void split(void) { argc=0; size_t i=0,n=length(line); while(i<n&&argc<ARG_MAX){while(i<n&&line[i]==' ')++i;if(i==n)break;argv[argc++]=line+i;while(i<n&&line[i]!=' ')++i;if(i<n)line[i++]=0;} }
static void prompt(void){ puts2("shirley:"); puts2(cwd); puts2("$ "); }
static void read_line(void){ size_t n=0; for(;;){char c; if(read(0,&c,1)!=1)continue; if(c=='\r'||c=='\n'){puts2("\n");break;} if((c==8||c==127)&&n){--n;puts2("\b \b");continue;} if(c>=' '&&c<='~'&&n+1<sizeof(line)){line[n++]=c;write(1,&c,1);}} line[n]=0; }

struct command { const char* name; void (*run)(void); const char* (*description)(void); };
static void help(void);

static const char* description_help(void){return "list commands and obtain this text from each command's description()";}
static const char* description_cd(void){return "change the shell working directory";}
static const char* description_pwd(void){return "print the shell working directory";}
static const char* description_echo(void){return "write text to stdout or redirect it to a path";}
static const char* description_clear(void){return "clear the terminal with ANSI escape sequences";}
static const char* description_ls(void){return "list a directory through the kernel VFS";}
static const char* description_cat(void){return "read a file through kernel file-descriptor syscalls";}
static const char* description_stat(void){return "query a VFS node's path, type, file system, and size";}
static const char* description_mount(void){return "list file systems mounted by the kernel";}
static const char* description_uptime(void){return "show kernel timer ticks and elapsed uptime";}
static const char* description_blk(void){return "read and dump one block from a kernel block device";}
static const char* description_exec(void){return "load and run a user-space ELF program by path";}
static const char* description_hello(void){return "run the user-space executable /bin/hello";}
static void ls(void){ char path[PATH_MAX]; struct node_info n; resolve(argc>1?argv[1]:".",path); if(shirley_syscall(6,path,&n)<0){putln("ls: not found");return;} if(n.type!=1){putln(n.name);return;} for(uint64_t i=0;shirley_syscall(7,path,i,&n)==0;++i){puts2("  ");puts2(n.name);if(n.type==1)puts2("/");puts2("\n");} }
static void cat_file(void){ if(argc<2){putln("cat: needs a file to print");return;} char path[PATH_MAX],b[256];resolve(argv[1],path);int fd=open(path,O_RDONLY);if(fd<0){putln("cat: open failed");return;}ssize_t n;while((n=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)n);close(fd); }
static void cd_cmd(void){ char path[PATH_MAX];struct node_info n;resolve(argc>1?argv[1]:"/",path);if(shirley_syscall(6,path,&n)<0){putln("cd: not found");return;}if(n.type!=1){putln("cd: not a directory");return;}copy(cwd,sizeof(cwd),n.path); }
static void stat_cmd(void){if(argc<2){putln("stat: needs a path");return;}char path[PATH_MAX];struct node_info n;resolve(argv[1],path);if(shirley_syscall(6,path,&n)<0){putln("stat: not found");return;}puts2("  path    ");putln(n.path);puts2("  type    ");putln(n.type==0?"file":n.type==1?"directory":n.type==2?"character":"block");puts2("  fs      ");putln(n.filesystem);puts2("  size    ");number(n.size);putln(" bytes");}
static void echo_cmd(void){int end=argc,fd=1;char path[PATH_MAX];for(int i=1;i<argc;++i)if(equal(argv[i],">")&&i+1<argc){end=i;resolve(argv[i+1],path);fd=open(path,O_WRONLY);if(fd<0){putln("echo: read-only file system");return;}break;}for(int i=1;i<end;++i){if(i>1)write(fd," ",1);write(fd,argv[i],length(argv[i]));}write(fd,"\n",1);if(fd!=1)close(fd);}
static void mount_cmd(void){struct mount_info m;for(uint64_t i=0;shirley_syscall(10,i,&m)==0;++i){puts2("  ");puts2(m.path);puts2("  ");putln(m.filesystem);}}
static void uptime_cmd(void){struct uptime_info u;if(shirley_syscall(8,&u)<0||!u.frequency){putln("uptime: no timer");return;}puts2("  up ");number(u.ticks/u.frequency);puts2(" s (");number(u.ticks);puts2(" timer interrupts at ");number(u.frequency);putln(" Hz)");}
static void blk_cmd(void){if(argc<3){putln("blk: needs a device and a block number");return;}char path[PATH_MAX];unsigned char b[512];uint64_t block=0;resolve(argv[1],path);for(char*p=argv[2];*p;++p)block=block*10+(uint64_t)(*p-'0');struct block_request r={path,block,b,sizeof(b)};long long n=shirley_syscall(11,&r);if(n<0){putln("blk: read failed");return;}const char*h="0123456789abcdef";for(int i=0;i<n;i+=16){for(int j=0;j<16&&i+j<n;++j){char x[3]={h[b[i+j]>>4],h[b[i+j]&15],' '};write(1,x,3);}puts2(" ");for(int j=0;j<16&&i+j<n;++j){char c=b[i+j]>=' '&&b[i+j]<='~'?(char)b[i+j]:'.';write(1,&c,1);}puts2("\n");}}
static void exec_cmd(const char* requested){char path[PATH_MAX];resolve(requested,path);long long status=shirley_syscall(9,path);puts2("[");puts2(path);puts2(" exited with status ");number((uint64_t)status&255);putln("]");}

static void pwd_cmd(void){putln(cwd);}
static void clear_cmd(void){puts2("\033[2J\033[H");}
static void hello_cmd(void){exec_cmd("/bin/hello");}
static void exec_command(void){if(argc<2){putln("exec: needs a program path");return;}exec_cmd(argv[1]);}

static const struct command shell_commands[]={
    {"help",help,description_help},{"cd",cd_cmd,description_cd},{"pwd",pwd_cmd,description_pwd},
    {"echo",echo_cmd,description_echo},{"clear",clear_cmd,description_clear}
};
static const struct command kernel_commands[]={
    {"ls",ls,description_ls},{"cat",cat_file,description_cat},{"stat",stat_cmd,description_stat},
    {"mount",mount_cmd,description_mount},{"uptime",uptime_cmd,description_uptime},{"blk",blk_cmd,description_blk}
};
static const struct command executable_commands[]={
    {"exec",exec_command,description_exec},{"hello",hello_cmd,description_hello}
};

static void help_group(const char* title,const struct command* commands,size_t count){
    putln(title);for(size_t i=0;i<count;++i){puts2("  ");puts2(commands[i].name);puts2(" - ");putln(commands[i].description());}
}
static void help(void){
    putln("ShirleyOS user-space shell:");
    help_group("User-space shell built-ins:",shell_commands,sizeof(shell_commands)/sizeof(shell_commands[0]));
    help_group("Kernel-backed commands:",kernel_commands,sizeof(kernel_commands)/sizeof(kernel_commands[0]));
    help_group("User-space executable commands:",executable_commands,sizeof(executable_commands)/sizeof(executable_commands[0]));
}
static int run_group(const struct command* commands,size_t count){for(size_t i=0;i<count;++i)if(equal(argv[0],commands[i].name)){commands[i].run();return 1;}return 0;}

int main(void){int fd=open("/etc/motd",O_RDONLY);if(fd>=0){char b[256];ssize_t n;while((n=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)n);close(fd);}putln("");for(;;){prompt();read_line();split();if(!argc)continue;if(run_group(shell_commands,sizeof(shell_commands)/sizeof(shell_commands[0])))continue;if(run_group(kernel_commands,sizeof(kernel_commands)/sizeof(kernel_commands[0])))continue;if(run_group(executable_commands,sizeof(executable_commands)/sizeof(executable_commands[0])))continue;puts2(argv[0]);putln(": unknown command (try help)");}}
