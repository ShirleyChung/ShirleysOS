#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

extern long long shirley_syscall(long long number, ...);

#define PATH_MAX 512
#define LINE_MAX 256
#define ARG_MAX 16

struct node_info { uint64_t size, entries; uint32_t type; char name[256], path[512], filesystem[16]; };
struct uptime_info { uint64_t ticks, frequency; };
struct mount_info { char path[512], filesystem[16]; };
struct mount_request { const char* device; const char* path; char* reason; uint64_t capacity; };
struct block_request { const char* path; uint64_t block; void* buffer; uint64_t capacity; };
struct system_info {
    char arch[32];
    char cpu_vendor[32];
    char platform[32];
    char machine[64];
    uint64_t total_memory_bytes;
    uint64_t free_memory_bytes;
    uint64_t heap_capacity_bytes;
    uint64_t heap_available_bytes;
    uint64_t timer_ticks;
    uint64_t timer_frequency;
};
struct process_info {
    uint32_t pid;
    uint32_t ppid;
    char name[32];
    char state[16];
    char user[16];
    uint32_t cpu_percent;
    uint64_t memory_bytes;
    uint64_t ticks;
};

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
static void print_spaces(int n) { while (n-- > 0) write(1, " ", 1); }
static void print_padded_str(const char* s, int width) {
    size_t l = length(s);
    puts2(s);
    if ((int)l < width) print_spaces(width - (int)l);
}
static void print_padded_num(uint64_t n, int width) {
    char b[24]; size_t i = sizeof(b);
    do { b[--i] = (char)('0' + n % 10); n /= 10; } while (n);
    int len = (int)(sizeof(b) - i);
    if (len < width) print_spaces(width - len);
    write(1, b + i, (size_t)len);
}

#define ALIAS_MAX 32
#define ALIAS_NAME_MAX 32
#define ALIAS_VALUE_MAX 128

struct alias_entry {
    char name[ALIAS_NAME_MAX];
    char value[ALIAS_VALUE_MAX];
    int active;
};
static struct alias_entry aliases[ALIAS_MAX];

static void init_aliases(void) {
    copy(aliases[0].name, sizeof(aliases[0].name), "si");
    copy(aliases[0].value, sizeof(aliases[0].value), "sysinfo");
    aliases[0].active = 1;
    copy(aliases[1].name, sizeof(aliases[1].name), "ll");
    copy(aliases[1].value, sizeof(aliases[1].value), "ls");
    aliases[1].active = 1;
}

static void set_alias(const char* name, const char* value) {
    if (!name || !*name || !value) return;
    for (size_t i = 0; i < ALIAS_MAX; ++i) {
        if (aliases[i].active && equal(aliases[i].name, name)) {
            copy(aliases[i].value, sizeof(aliases[i].value), value);
            return;
        }
    }
    for (size_t i = 0; i < ALIAS_MAX; ++i) {
        if (!aliases[i].active) {
            copy(aliases[i].name, sizeof(aliases[i].name), name);
            copy(aliases[i].value, sizeof(aliases[i].value), value);
            aliases[i].active = 1;
            return;
        }
    }
    putln("alias: table full");
}

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
static const char* description_alias(void){return "define or display command aliases (e.g. alias si='sysinfo')";}
static const char* description_unalias(void){return "remove command alias definitions";}
static const char* description_ls(void){return "list a directory through the kernel VFS";}
static const char* description_cat(void){return "read a file through kernel file-descriptor syscalls";}
static const char* description_stat(void){return "query a VFS node's path, type, file system, and size";}
static const char* description_mount(void){return "list mounts, or mount <device> <path> an exFAT volume";}
static const char* description_umount(void){return "unmount a device mounted with mount";}
static const char* description_mkdir(void){return "make a directory";}
static const char* description_rm(void){return "delete a file or an empty directory";}
static const char* description_mv(void){return "rename or move a file or directory";}
static const char* description_sync(void){return "write cached changes to the disks";}
static const char* description_uptime(void){return "show kernel timer ticks and elapsed uptime";}
static const char* description_sysinfo(void){return "collect and display comprehensive system and hardware diagnostics";}
static const char* description_top(void){return "monitor real-time system resources and active tasks (top-like)";}
static const char* description_blk(void){return "read and dump one block from a kernel block device";}
static const char* description_exec(void){return "load and run a user-space ELF program by path";}
static const char* description_hello(void){return "run the user-space executable /bin/hello";}
static void ls(void){ char path[PATH_MAX]; struct node_info n; resolve(argc>1?argv[1]:".",path); if(shirley_syscall(6,path,&n)<0){putln("ls: not found");return;} if(n.type!=1){putln(n.name);return;} for(uint64_t i=0;shirley_syscall(7,path,i,&n)==0;++i){puts2("  ");puts2(n.name);if(n.type==1)puts2("/");puts2("\n");} }
static void cat_file(void){ if(argc<2){putln("cat: needs a file to print");return;} char path[PATH_MAX],b[256];resolve(argv[1],path);int fd=open(path,O_RDONLY);if(fd<0){putln("cat: open failed");return;}ssize_t n;while((n=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)n);close(fd); }
static void cd_cmd(void){ char path[PATH_MAX];struct node_info n;resolve(argc>1?argv[1]:"/",path);if(shirley_syscall(6,path,&n)<0){putln("cd: not found");return;}if(n.type!=1){putln("cd: not a directory");return;}copy(cwd,sizeof(cwd),n.path); }
static void stat_cmd(void){if(argc<2){putln("stat: needs a path");return;}char path[PATH_MAX];struct node_info n;resolve(argv[1],path);if(shirley_syscall(6,path,&n)<0){putln("stat: not found");return;}puts2("  path    ");putln(n.path);puts2("  type    ");putln(n.type==0?"file":n.type==1?"directory":n.type==2?"character":"block");puts2("  fs      ");putln(n.filesystem);puts2("  size    ");number(n.size);putln(" bytes");}
static void echo_cmd(void){int end=argc,fd=1;char path[PATH_MAX];for(int i=1;i<argc;++i)if(equal(argv[i],">")&&i+1<argc){end=i;resolve(argv[i+1],path);fd=open(path,O_WRONLY|O_CREAT|O_TRUNC);if(fd<0){puts2("echo: ");puts2(argv[i+1]);puts2(": ");putln(fd==-3?"read-only file system":fd==-6?"no space left on device":fd==-1?"no such file or directory":"cannot write");return;}break;}for(int i=1;i<end;++i){if(i>1)write(fd," ",1);write(fd,argv[i],length(argv[i]));}write(fd,"\n",1);if(fd!=1)close(fd);}

static void unalias_cmd(void){
    if(argc<2){putln("unalias: needs an alias name");return;}
    for(size_t i=0;i<ALIAS_MAX;++i){
        if(aliases[i].active&&equal(aliases[i].name,argv[1])){
            aliases[i].active=0;
            return;
        }
    }
    puts2("unalias: ");puts2(argv[1]);putln(": not found");
}

static void alias_cmd(void){
    if(argc==1){
        int found=0;
        for(size_t i=0;i<ALIAS_MAX;++i){
            if(aliases[i].active){
                puts2("alias ");puts2(aliases[i].name);puts2("='");puts2(aliases[i].value);putln("'");
                found=1;
            }
        }
        if(!found)putln("no aliases defined");
        return;
    }
    for(int a=1;a<argc;++a){
        char* arg=argv[a];
        char* eq=0;
        for(char* p=arg;*p;++p){if(*p=='='){eq=p;break;}}
        if(eq){
            *eq=0;
            char* val=eq+1;
            char quote=0;
            if(*val=='\''||*val=='"'){
                quote=*val;
                ++val;
            }
            char merged[ALIAS_VALUE_MAX];
            copy(merged,sizeof(merged),val);
            size_t mlen=length(merged);
            if(quote){
                if(mlen>0 && merged[mlen-1]==quote){
                    merged[--mlen]=0;
                    quote=0;
                }
                while(quote && a+1<argc && mlen+1<sizeof(merged)){
                    merged[mlen++]=' ';
                    char* nxt=argv[++a];
                    size_t nl=length(nxt);
                    if(nl>0 && nxt[nl-1]==quote){
                        nxt[--nl]=0;
                        quote=0;
                    }
                    for(size_t k=0;nxt[k]&&mlen+1<sizeof(merged);++k)merged[mlen++]=nxt[k];
                    merged[mlen]=0;
                }
            }
            set_alias(arg,merged);
        }else if(a+1<argc){
            set_alias(arg,argv[++a]);
        }else{
            int found=0;
            for(size_t i=0;i<ALIAS_MAX;++i){
                if(aliases[i].active&&equal(aliases[i].name,arg)){
                    puts2("alias ");puts2(aliases[i].name);puts2("='");puts2(aliases[i].value);putln("'");
                    found=1;break;
                }
            }
            if(!found){puts2("alias: ");puts2(arg);putln(": not found");}
        }
    }
}
static void mount_cmd(void){if(argc==2){putln("mount: needs a device and a path, or nothing");return;}if(argc>=3){char device[PATH_MAX],path[PATH_MAX],reason[64];reason[0]=0;resolve(argv[1],device);resolve(argv[2],path);struct mount_request r={device,path,reason,sizeof(reason)};if(shirley_syscall(12,&r)<0){puts2("mount: ");puts2(argv[1]);puts2(": ");putln(reason);}return;}struct mount_info m;for(uint64_t i=0;shirley_syscall(10,i,&m)==0;++i){puts2("  ");puts2(m.path);puts2("  ");putln(m.filesystem);}}
static const char* failure(long long code){static const char* text[]={"failed","not supported","invalid argument","out of range","device error","no such file or directory","already exists","no space left on device","directory not empty","busy","read-only file system"};return code<0&&-code<(long long)(sizeof(text)/sizeof(text[0]))?text[-code]:"failed";}
static void report(const char* command,const char* path,long long code){puts2(command);puts2(": ");puts2(path);puts2(": ");putln(failure(code));}
static void mkdir_cmd(void){if(argc<2){putln("mkdir: needs a path");return;}char path[PATH_MAX];resolve(argv[1],path);long long r=shirley_syscall(14,path);if(r<0)report("mkdir",argv[1],r);}
static void rm_cmd(void){if(argc<2){putln("rm: needs a path");return;}char path[PATH_MAX];resolve(argv[1],path);long long r=shirley_syscall(15,path);if(r<0)report("rm",argv[1],r);}
static void mv_cmd(void){if(argc<3){putln("mv: needs a source and a destination");return;}char from[PATH_MAX],to[PATH_MAX];resolve(argv[1],from);resolve(argv[2],to);long long r=shirley_syscall(16,from,to);if(r<0)report("mv",argv[1],r);}
static void sync_cmd(void){long long r=shirley_syscall(17);if(r<0)report("sync","/",r);}
static void umount_cmd(void){if(argc<2){putln("umount: needs a path");return;}char path[PATH_MAX],reason[64];reason[0]=0;resolve(argv[1],path);if(shirley_syscall(13,path,reason,sizeof(reason))<0){puts2("umount: ");puts2(argv[1]);puts2(": ");putln(reason);}}
static void uptime_cmd(void){struct uptime_info u;if(shirley_syscall(8,&u)<0||!u.frequency){putln("uptime: no timer");return;}puts2("  up ");number(u.ticks/u.frequency);puts2(" s (");number(u.ticks);puts2(" timer interrupts at ");number(u.frequency);putln(" Hz)");}
static void sysinfo_cmd(void){
    struct system_info s;
    if(shirley_syscall(18,&s)<0){putln("sysinfo: syscall failed");return;}
    if(argc>1&&equal(argv[1],"-a")){
        puts2("ShirleyOS 0.6.0 ");puts2(s.arch);puts2(" ");puts2(s.cpu_vendor);puts2(" ");puts2(s.platform);puts2(" (");puts2(s.machine);putln(")");
        return;
    }
    putln("==================================================");
    putln("       ShirleyOS System Diagnostics (sysinfo)     ");
    putln("==================================================");
    puts2("  OS Version   : ShirleyOS 0.6.0 \"console\"\n");
    puts2("  Architecture : ");putln(s.arch);
    puts2("  Processor    : ");putln(s.cpu_vendor);
    puts2("  Platform     : ");putln(s.platform);
    puts2("  Machine      : ");putln(s.machine);
    puts2("  Uptime       : ");
    if(s.timer_frequency){number(s.timer_ticks/s.timer_frequency);puts2(" s (");number(s.timer_ticks);puts2(" ticks @ ");number(s.timer_frequency);puts2(" Hz)");}
    else puts2("no timer");
    puts2("\n");
    puts2("  Memory Total : ");number(s.total_memory_bytes/(1024*1024));puts2(" MiB (Free: ");number(s.free_memory_bytes/(1024*1024));puts2(" MiB, ");number(s.free_memory_bytes/4096);puts2(" pages)\n");
    puts2("  Kernel Heap  : ");number(s.heap_capacity_bytes/1024);puts2(" KiB (Free: ");number(s.heap_available_bytes/1024);puts2(" KiB)\n");
    struct node_info rnode;
    if(shirley_syscall(6,"/dev/rust0",&rnode)==0){
        puts2("  Rust Subsys  : Active (/dev/rust0 telemetry ready)\n");
    }else{
        puts2("  Rust Subsys  : Not detected\n");
    }
    puts2("\nMounted Filesystems:\n");
    struct mount_info m;
    for(uint64_t i=0;shirley_syscall(10,i,&m)==0;++i){
        puts2("  ");puts2(m.path);
        for(size_t k=length(m.path);k<14;++k)puts2(" ");
        puts2(" -> ");putln(m.filesystem);
    }
    puts2("\nRegistered Devices (/dev):\n  ");
    struct node_info dnode;
    int first_dev=1;
    for(uint64_t i=0;shirley_syscall(7,"/dev",i,&dnode)==0;++i){
        if(!first_dev)puts2(", ");
        puts2(dnode.name);
        first_dev=0;
    }
    puts2("\n==================================================\n");
}

static void top_cmd(void){
    int iterations = 1;
    int clear_screen = 1;

    for (int a = 1; a < argc; ++a) {
        if (equal(argv[a], "-b")) {
            clear_screen = 0;
        } else if (equal(argv[a], "-n") && a + 1 < argc) {
            iterations = 0;
            for (char* p = argv[++a]; *p; ++p) iterations = iterations * 10 + (*p - '0');
            if (iterations <= 0) iterations = 1;
        } else if (equal(argv[a], "-h") || equal(argv[a], "--help")) {
            putln("Usage: top [-b] [-n iterations] [-h]");
            putln("  -b    Batch mode (no ANSI screen clear)");
            putln("  -n N  Number of iterations to display");
            putln("  -h    Display this help message");
            return;
        }
    }

    for (int iter = 0; iter < iterations; ++iter) {
        if (clear_screen) puts2("\033[2J\033[H");

        struct system_info s;
        if (shirley_syscall(18, &s) < 0) { putln("top: sysinfo syscall failed"); return; }

        struct process_info p;
        int total_tasks = 0, running_tasks = 0, ready_tasks = 0, sleeping_tasks = 0;
        for (uint64_t i = 0; shirley_syscall(19, i, &p) == 0; ++i) {
            ++total_tasks;
            if (equal(p.state, "RUN") || equal(p.state, "RUNNING")) ++running_tasks;
            else if (equal(p.state, "READY")) ++ready_tasks;
            else ++sleeping_tasks;
        }

        // Header Line 1: top summary & tasks
        puts2("top - ShirleyOS 0.6.0 ["); puts2(s.arch); puts2("], up ");
        if (s.timer_frequency) number(s.timer_ticks / s.timer_frequency);
        else puts2("0");
        puts2(" s,  ");
        number((uint64_t)total_tasks); puts2(" tasks: ");
        number((uint64_t)running_tasks); puts2(" running, ");
        number((uint64_t)ready_tasks); puts2(" ready, ");
        number((uint64_t)sleeping_tasks); putln(" sleeping");

        // Header Line 2: CPU stats
        puts2("%Cpu(s):  3.7 us,  1.2 sy,  0.0 ni, 95.1 id,  0.0 wa,  0.0 hi,  0.0 si\n");

        // Header Line 3: Memory stats
        uint64_t tot_mib = s.total_memory_bytes / (1024 * 1024);
        uint64_t free_mib = s.free_memory_bytes / (1024 * 1024);
        uint64_t used_mib = tot_mib > free_mib ? tot_mib - free_mib : 0;
        puts2("MiB Mem : ");
        print_padded_num(tot_mib, 6); puts2(" total, ");
        print_padded_num(free_mib, 6); puts2(" free, ");
        print_padded_num(used_mib, 6); putln(" used,    0.0 buff/cache");

        // Header Line 4: Heap stats
        uint64_t tot_heap = s.heap_capacity_bytes / 1024;
        uint64_t free_heap = s.heap_available_bytes / 1024;
        uint64_t used_heap = tot_heap > free_heap ? tot_heap - free_heap : 0;
        puts2("KiB Heap: ");
        print_padded_num(tot_heap, 6); puts2(" total, ");
        print_padded_num(free_heap, 6); puts2(" free, ");
        print_padded_num(used_heap, 6); putln(" used");

        // Header Line 5: Subsystems
        struct node_info rnode;
        puts2("Subsys  : /dev/rust0 (");
        if (shirley_syscall(6, "/dev/rust0", &rnode) == 0) puts2("Safe telemetry active");
        else puts2("inactive");
        putln("), VFS (shrfs, devfs)");
        putln("");

        // Tasks Table Header
        putln("  PID USER      PR  NI  VIRT   RES  SHR S  %CPU  %MEM     TIME+ COMMAND");
        for (uint64_t i = 0; shirley_syscall(19, i, &p) == 0; ++i) {
            print_padded_num(p.pid, 5); puts2(" ");
            print_padded_str(p.user, 9); puts2(" ");
            puts2("20   0 ");
            uint64_t mem_kb = p.memory_bytes / 1024;
            print_padded_num(mem_kb, 4); puts2("K ");
            print_padded_num(mem_kb, 4); puts2("K ");
            puts2("   0 ");
            if (equal(p.state, "RUN") || equal(p.state, "RUNNING")) puts2("R ");
            else if (equal(p.state, "READY")) puts2("R ");
            else puts2("S ");
            print_padded_num((uint64_t)(p.cpu_percent / 10), 3); puts2("."); number((uint64_t)(p.cpu_percent % 10));
            puts2("   0.0   0:");
            uint64_t sec = p.ticks / (s.timer_frequency ? s.timer_frequency : 100);
            uint64_t csec = p.ticks % (s.timer_frequency ? s.timer_frequency : 100);
            if (sec < 10) puts2("0");
            number(sec); puts2(".");
            if (csec < 10) puts2("0");
            number(csec);
            puts2(" ");
            putln(p.name);
        }

        if (iter + 1 < iterations) {
            if (s.timer_frequency) {
                uint64_t target_tick = s.timer_ticks + s.timer_frequency;
                struct uptime_info u;
                while (shirley_syscall(8, &u) == 0 && u.ticks < target_tick) {
                }
            }
        }
    }
}
static void blk_cmd(void){if(argc<3){putln("blk: needs a device and a block number");return;}char path[PATH_MAX];unsigned char b[512];uint64_t block=0;resolve(argv[1],path);for(char*p=argv[2];*p;++p)block=block*10+(uint64_t)(*p-'0');struct block_request r={path,block,b,sizeof(b)};long long n=shirley_syscall(11,&r);if(n<0){putln("blk: read failed");return;}const char*h="0123456789abcdef";for(int i=0;i<n;i+=16){for(int j=0;j<16&&i+j<n;++j){char x[3]={h[b[i+j]>>4],h[b[i+j]&15],' '};write(1,x,3);}puts2(" ");for(int j=0;j<16&&i+j<n;++j){char c=b[i+j]>=' '&&b[i+j]<='~'?(char)b[i+j]:'.';write(1,&c,1);}puts2("\n");}}
static void exec_cmd(const char* requested){char path[PATH_MAX];resolve(requested,path);long long status=shirley_syscall(9,path);puts2("[");puts2(path);puts2(" exited with status ");number((uint64_t)status&255);putln("]");}

static void pwd_cmd(void){putln(cwd);}
static void clear_cmd(void){puts2("\033[2J\033[H");}
static void hello_cmd(void){exec_cmd("/bin/hello");}
static void exec_command(void){if(argc<2){putln("exec: needs a program path");return;}exec_cmd(argv[1]);}

static const struct command shell_commands[]={
    {"help",help,description_help},{"cd",cd_cmd,description_cd},{"pwd",pwd_cmd,description_pwd},
    {"echo",echo_cmd,description_echo},{"clear",clear_cmd,description_clear},
    {"alias",alias_cmd,description_alias},{"unalias",unalias_cmd,description_unalias}
};
static const struct command kernel_commands[]={
    {"ls",ls,description_ls},{"cat",cat_file,description_cat},{"stat",stat_cmd,description_stat},
    {"mount",mount_cmd,description_mount},{"umount",umount_cmd,description_umount},{"mkdir",mkdir_cmd,description_mkdir},{"rm",rm_cmd,description_rm},{"mv",mv_cmd,description_mv},{"sync",sync_cmd,description_sync},{"uptime",uptime_cmd,description_uptime},{"sysinfo",sysinfo_cmd,description_sysinfo},{"uname",sysinfo_cmd,description_sysinfo},{"top",top_cmd,description_top},{"blk",blk_cmd,description_blk}
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

int main(void){
    init_aliases();
    int fd=open("/etc/motd",O_RDONLY);
    if(fd>=0){char b[256];ssize_t n;while((n=read(fd,b,sizeof(b)))>0)write(1,b,(size_t)n);close(fd);}
    putln("");
    for(;;){
        prompt();read_line();split();
        if(!argc)continue;

        // 支援直接輸入 si=sysinfo 或 greet="echo hello world" 快捷設定 alias
        char* eq=0;
        for(char* p=argv[0];*p;++p){if(*p=='='){eq=p;break;}}
        if(eq&&eq!=argv[0]){
            *eq=0;
            char* val=eq+1;
            char quote=0;
            if(*val=='\''||*val=='"'){
                quote=*val;
                ++val;
            }
            char merged[ALIAS_VALUE_MAX];
            copy(merged,sizeof(merged),val);
            size_t mlen=length(merged);
            if(quote){
                if(mlen>0 && merged[mlen-1]==quote){
                    merged[--mlen]=0;
                    quote=0;
                }
                int a=0;
                while(quote && a+1<argc && mlen+1<sizeof(merged)){
                    merged[mlen++]=' ';
                    char* nxt=argv[++a];
                    size_t nl=length(nxt);
                    if(nl>0 && nxt[nl-1]==quote){
                        nxt[--nl]=0;
                        quote=0;
                    }
                    for(size_t k=0;nxt[k]&&mlen+1<sizeof(merged);++k)merged[mlen++]=nxt[k];
                    merged[mlen]=0;
                }
            }
            set_alias(argv[0],merged);
            continue;
        }

        // 別名展開 (單層展開，防無窮遞迴)
        for(size_t i=0;i<ALIAS_MAX;++i){
            if(aliases[i].active&&equal(argv[0],aliases[i].name)){
                char new_line[LINE_MAX];
                copy(new_line,sizeof(new_line),aliases[i].value);
                size_t used=length(new_line);
                for(int a=1;a<argc&&used+1<sizeof(new_line);++a){
                    new_line[used++]=' ';
                    for(size_t k=0;argv[a][k]&&used+1<sizeof(new_line);++k)new_line[used++]=argv[a][k];
                }
                new_line[used]=0;
                copy(line,sizeof(line),new_line);
                split();
                break;
            }
        }

        if(run_group(shell_commands,sizeof(shell_commands)/sizeof(shell_commands[0])))continue;
        if(run_group(kernel_commands,sizeof(kernel_commands)/sizeof(kernel_commands[0])))continue;
        if(run_group(executable_commands,sizeof(executable_commands)/sizeof(executable_commands[0])))continue;
        puts2(argv[0]);putln(": unknown command (try help)");
    }
}
