#include "shirley/shell.hpp"

#include "shirley/arch.hpp"
#include "shirley/console.hpp"
#include "shirley/device.hpp"
#include "shirley/format.hpp"
#include "shirley/fs.hpp"
#include "shirley/io.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/text.hpp"
#include "shirley/user_loader.hpp"
#include "shirley/vfs.hpp"

namespace shirley::shell {
namespace {

// 一行指令的長度上限，以及一行最多能拆出幾個引數。兩者都刻意設得小：
// 超出上限的輸入會被忽略，而不是讓緩衝區溢位。
//
// The longest command line and how many arguments one line can hold. Both are
// deliberately small, and input past either limit is ignored rather than
// allowed to overrun a buffer.
constexpr std::size_t line_capacity = 128;
constexpr std::size_t max_arguments = 8;
// cat 每次從檔案讀出的位元組數。整個檔案不需要一次進記憶體，因此讀多少就
// 輸出多少。
//
// How many bytes cat reads at a time. A whole file never needs to be in memory
// at once, so each chunk is written out as soon as it arrives.
constexpr std::size_t read_chunk = 128;

char line[line_capacity];
std::size_t line_length = 0;
char* arguments[max_arguments];
std::size_t argument_count = 0;
char working_directory[vfs::max_path_length] = "/";

void write(const char* text) { console::write(text); }

void write_line(const char* text) {
    console::write(text);
    console::write("\n");
}

void write_number(std::uint64_t value) {
    // 64 位元十進位最多 20 位數，加上結尾。
    // A 64-bit decimal value needs at most 20 digits, plus the terminator.
    char digits[21];
    format::to_decimal(digits, sizeof(digits), value);
    write(digits);
}

// 靠右對齊輸出數值，讓 ls 的檔案大小排成一欄。
// Write a value right-aligned so ls lines its file sizes up in a column.
void write_number_padded(std::uint64_t value, std::size_t width) {
    char digits[21];
    const auto length = format::to_decimal(digits, sizeof(digits), value);
    for (std::size_t i = length; i < width; ++i) write(" ");
    write(digits);
}

void write_padded(const char* text, std::size_t width) {
    const auto length = text::length(text);
    for (std::size_t i = length; i < width; ++i) write(" ");
    write(text);
}

// 解析使用者輸入的路徑：絕對路徑從根目錄開始，其餘從工作目錄開始。走的是
// VFS，因此 /dev 底下的裝置和根檔案系統裡的檔案用的是同一個名字空間，shell
// 不必知道它們分屬兩個檔案系統。
//
// Resolve a path the user typed: an absolute one from the root, anything else
// from the working directory. It goes through the VFS, so a device under /dev
// and a file in the root file system share one namespace and the shell never
// needs to know they belong to two different file systems.
bool resolve(const char* path, vfs::Node& node) {
    return vfs::stat(path, node, working_directory);
}

void report_missing(const char* command, const char* path) {
    write(command);
    write(": ");
    write(path);
    write_line(": no such file or directory");
}

// 讀出一個字元。佇列是空的時候回傳 false，呼叫端會去等下一個中斷。
// Take one character. An empty queue returns false and the caller waits for
// the next interrupt.
bool read_character(char& value) {
    const auto result = io::read_standard_input(&value, 1);
    return static_cast<bool>(result) && result.transferred == 1;
}

// 行編輯器。回顯在這裡而不在驅動程式裡：畫面上該出現什麼，只有正在收集這
// 一行的人知道——例如空行時的 backspace 必須什麼都不做，否則會把提示符擦掉。
//
// The line editor. Echo lives here rather than in the driver, because only
// whoever is collecting the line knows what should appear on screen — a
// backspace on an empty line, for instance, must do nothing at all or it would
// erase the prompt.
void read_line() {
    line_length = 0;
    line[0] = '\0';
    for (;;) {
        char value = '\0';
        if (!read_character(value)) {
            // 沒有字元可讀就停在低功耗狀態等下一個中斷。按鍵中斷可能剛好在
            // 檢查與等待之間送達，那一次會多等到下一個計時器中斷才醒來，
            // 也就是最多 10 毫秒；輪詢鍵盤換來的那點延遲不值得。
            //
            // With nothing to read, park in a low-power state until the next
            // interrupt. A keystroke can arrive between the check and the
            // wait, in which case this pass sleeps until the next timer
            // interrupt instead — at most 10 ms. That is not worth polling the
            // keyboard to avoid.
            arch::wait_for_interrupt();
            continue;
        }
        // 換行與回車都算送出這一行：終端機按 Enter 送的是回車符，鍵盤驅動
        // 程式解出來的是換行，行編輯器兩個都收才不必在意輸入來自哪裡。
        //
        // Both newline and carriage return end the line. A terminal sends
        // carriage return for Enter while the keyboard driver decodes a
        // newline, and accepting either means the line editor does not care
        // where the input came from.
        if (value == '\n' || value == '\r') {
            write("\n");
            line[line_length] = '\0';
            return;
        }
        if (value == '\b') {
            if (line_length == 0) continue;
            --line_length;
            line[line_length] = '\0';
            // 真的把畫面上的字元擦掉：退一格、寫空白、再退一格。
            // Actually erase the character on screen: back up, overwrite with
            // a space, back up again.
            write("\b \b");
            continue;
        }
        // 只接受可列印的 ASCII。其他控制字元（跳脫序列、Tab）目前沒有意義，
        // 收下來只會讓行內容和畫面對不起來。
        //
        // Only printable ASCII is accepted. Other control characters — escape
        // sequences, Tab — mean nothing yet, and taking them would only make
        // the line disagree with the screen.
        if (value < ' ' || value > '~') continue;
        if (line_length + 1 >= line_capacity) continue;
        line[line_length++] = value;
        line[line_length] = '\0';
        console::write(&value, 1);
    }
}

// 就地把一行拆成引數：空白改寫成結尾符號，引數指向行內的字串。
// Split the line in place: spaces become terminators and each argument points
// into the line itself.
void split_line() {
    argument_count = 0;
    std::size_t index = 0;
    while (index < line_length && argument_count < max_arguments) {
        while (index < line_length && line[index] == ' ') ++index;
        if (index >= line_length) break;
        arguments[argument_count++] = &line[index];
        while (index < line_length && line[index] != ' ') ++index;
        if (index < line_length) line[index++] = '\0';
    }
}

void command_help() {
    write_line("ShirleyOS console commands:");
    write_line("  help              this list");
    write_line("  ls [path]         list a directory, or the working directory");
    write_line("  cat <file>        print a file");
    write_line("  cd [path]         change the working directory, or go to /");
    write_line("  pwd               print the working directory");
    write_line("  stat <path>       show one entry's details");
    write_line("  echo [text] [> p] print the arguments, or write them to a path");
    write_line("  mem               page allocator totals");
    write_line("  uptime            time since boot, counted in timer interrupts");
    write_line("  devices           list the registered devices");
    write_line("  mount             list the mounted file systems");
    write_line("  blk <dev> <n>     dump block n of a block device, such as /dev/ram0");
    write_line("  version           kernel, architecture, and platform");
    write_line("  clear             clear the screen");
    write_line("  exec <path>       load and run a program from the file system");
    write_line("  hello             run /bin/hello");
    write_line("  reboot            restart the machine");
    write_line("  poweroff          shut the machine down");
}

// 目錄與裝置沒有「大小」可以印，因此那一欄放的是它們是什麼。字元裝置的長度
// 是沒有意義的問題，區塊裝置的長度則是容量，那個數字有意義所以照印。
//
// A directory and a character device have no size to print, so that column
// says what they are instead. How long a keyboard is, is not a question; a
// block device's length is its capacity, which is a real number and is shown.
void list_entry(const vfs::Node& node) {
    switch (node.type) {
    case vfs::Type::Directory: write_padded("<dir>", 8); break;
    case vfs::Type::CharacterDevice: write_padded("<char>", 8); break;
    default: write_number_padded(node.size, 8); break;
    }
    write("  ");
    write(node.name);
    if (node.directory()) write("/");
    write("\n");
}

void command_ls() {
    const char* path = argument_count > 1 ? arguments[1] : ".";
    vfs::Node node;
    if (!resolve(path, node)) {
        report_missing("ls", path);
        return;
    }
    // 指向檔案的 ls 就是列出那一個檔案，和列出目錄用同一種格式。
    // An ls naming a file lists that one file, in the same format a directory
    // listing uses.
    if (!node.directory()) {
        list_entry(node);
        return;
    }
    vfs::Node child;
    std::size_t position = 0;
    while (vfs::list(node, position, child)) {
        list_entry(child);
        ++position;
    }
    write_number(position);
    write_line(position == 1 ? " entry" : " entries");
}

void command_cat() {
    if (argument_count < 2) {
        write_line("cat: needs a file to print");
        return;
    }
    for (std::size_t index = 1; index < argument_count; ++index) {
        const char* path = arguments[index];
        // 開啟、讀到結尾、關閉。這條路和 ELF loader 讀 /bin/hello 走的是同一條，
        // 對 /dev 底下的裝置也一樣有效——cat 不知道自己讀的是檔案還是裝置。
        //
        // Open, read to the end, close. This is the same path the ELF loader
        // takes to read /bin/hello, and it works just as well on a device under
        // /dev: cat does not know which of the two it is reading.
        const auto descriptor = vfs::open(path, vfs::OpenFlags::Read, working_directory);
        if (descriptor < 0) {
            write("cat: ");
            write(path);
            write(": ");
            write_line(vfs::error_text(descriptor));
            continue;
        }
        char buffer[read_chunk];
        for (;;) {
            const auto result = vfs::read(descriptor, buffer, sizeof(buffer));
            if (!result) {
                write("cat: ");
                write(path);
                write_line(": read failed");
                break;
            }
            // 讀到 0 個位元組就是結束了：一般檔案讀到了結尾，字元裝置則是
            // 現在沒有東西可讀。
            //
            // Zero bytes means this is done: an ordinary file reached its end,
            // and a character device has nothing to give right now.
            if (result.transferred == 0) break;
            console::write(buffer, result.transferred);
        }
        vfs::close(descriptor);
    }
}

void command_cd() {
    const char* path = argument_count > 1 ? arguments[1] : "/";
    vfs::Node node;
    if (!resolve(path, node)) {
        report_missing("cd", path);
        return;
    }
    if (!node.directory()) {
        write("cd: ");
        write(path);
        write_line(": not a directory");
        return;
    }
    // 節點自己帶著正規化後的絕對路徑，因此提示符顯示的永遠是那個路徑，
    // 而不是使用者打進來的那串 "../.."。
    //
    // A node carries its own normalized absolute path, so the prompt shows
    // that rather than the "../.." the user typed.
    text::copy(working_directory, sizeof(working_directory), node.path);
}

void command_pwd() { write_line(working_directory); }

void command_stat() {
    if (argument_count < 2) {
        write_line("stat: needs a path");
        return;
    }
    vfs::Node node;
    if (!resolve(arguments[1], node)) {
        report_missing("stat", arguments[1]);
        return;
    }
    write("  path    ");
    write_line(node.path);
    write("  type    ");
    write_line(vfs::type_name(node.type));
    write("  fs      ");
    write_line(node.filesystem != nullptr ? node.filesystem->name() : "none");
    if (node.directory()) {
        write("  entries ");
        write_number(vfs::child_count(node));
        write("\n");
        return;
    }
    write("  size    ");
    write_number(node.size);
    write_line(" bytes");
    // 區塊裝置多印它的幾何：檔案系統關心的是有幾個區塊、一塊多大，那比
    // 總位元組數更接近它實際要下的指令。
    //
    // A block device also shows its geometry: how many blocks there are and
    // how big one is, which is closer to what a file system actually asks for
    // than a total byte count.
    if (node.type == vfs::Type::BlockDevice && node.device != nullptr) {
        write("  blocks  ");
        write_number(node.device->block_count());
        write(" of ");
        write_number(node.device->block_size());
        write_line(" bytes");
    }
}

// 目前掛了哪些檔案系統。這是 VFS 唯一的狀態，看得到它才知道一個路徑會被
// 誰回答。
//
// Which file systems are mounted. This is the VFS's only state, and seeing it
// is what tells you who will answer for a given path.
void command_mount() {
    for (std::size_t index = 0; index < vfs::mount_count(); ++index) {
        write("  ");
        const char* path = vfs::mount_path(index);
        write(path);
        for (auto column = text::length(path); column < 10; ++column) write(" ");
        auto* filesystem = vfs::mount_filesystem(index);
        write_line(filesystem != nullptr ? filesystem->name() : "none");
    }
}

// echo，可以把輸出導到一個路徑上。導向存在的理由不是方便，而是它是 shell
// 裡唯一會呼叫 vfs::write() 的地方：`echo hi > /dev/uart0` 真的把兩個字元送
// 出序列埠，`> /dev/null` 收下並丟掉，`> /etc/version` 則在 open() 就被拒絕，
// 因為 SHRFS1 是唯讀的。
//
// echo, with its output redirectable to a path. Redirection is here not for
// convenience but because it is the one place in the shell that calls
// vfs::write(): `echo hi > /dev/uart0` really does put two characters on the
// serial line, `> /dev/null` accepts and discards them, and `> /etc/version`
// is refused at open() because SHRFS1 is read-only.
void command_echo() {
    std::size_t last = argument_count;
    const char* target = nullptr;
    for (std::size_t index = 1; index < argument_count; ++index) {
        if (!text::equals(arguments[index], ">")) continue;
        if (index + 1 >= argument_count) {
            write_line("echo: > needs a path to write to");
            return;
        }
        last = index;
        target = arguments[index + 1];
        break;
    }

    // 先把要輸出的那一行組起來，再決定送到哪裡。兩條路寫的是同一串位元組。
    // Assemble the line first and decide where it goes afterwards. Both paths
    // write the very same bytes.
    char line_out[line_capacity];
    line_out[0] = '\0';
    for (std::size_t index = 1; index < last; ++index) {
        if (index > 1 && !text::append(line_out, sizeof(line_out), ' ')) break;
        if (!text::append(line_out, sizeof(line_out), arguments[index])) break;
    }
    text::append(line_out, sizeof(line_out), '\n');

    if (target == nullptr) {
        write(line_out);
        return;
    }
    const auto descriptor = vfs::open(target, vfs::OpenFlags::Write, working_directory);
    if (descriptor < 0) {
        write("echo: ");
        write(target);
        write(": ");
        write_line(vfs::error_text(descriptor));
        return;
    }
    const auto result = vfs::write(descriptor, line_out, text::length(line_out));
    vfs::close(descriptor);
    if (!result) {
        write("echo: ");
        write(target);
        write_line(": write failed");
    }
}

void command_mem() {
    write("  page size   ");
    write_number(memory::page_size);
    write_line(" bytes");
    write("  total       ");
    write_number(memory::total_pages());
    write(" pages, ");
    write_number(static_cast<std::uint64_t>(memory::total_pages()) * memory::page_size / (1024 * 1024));
    write_line(" MiB");
    write("  free        ");
    write_number(memory::free_pages());
    write(" pages, ");
    write_number(static_cast<std::uint64_t>(memory::free_pages()) * memory::page_size / (1024 * 1024));
    write_line(" MiB");
}

void command_uptime() {
    const auto rate = platform::timer_frequency();
    const auto ticks = platform::timer_ticks();
    if (rate == 0) {
        write_line("uptime: this platform has no timer");
        return;
    }
    write("  up ");
    write_number(ticks / rate);
    write(" s (");
    write_number(ticks);
    write(" timer interrupts at ");
    write_number(rate);
    write_line(" Hz)");
}

void command_version() {
    write("  ShirleyOS on ");
    write(arch::name());
    write(", ");
    write_line(platform::machine());
    write("  file system ");
    if (!fs::mounted()) {
        write_line("not mounted");
        return;
    }
    write_number(fs::entry_count());
    write(" entries, ");
    write_number(fs::total_file_bytes());
    write_line(" bytes of files");
}

// 這台機器目前有哪些裝置，以及主控台的輸入是從哪些裝置來的。shell 只認得
// 裝置的名字與種類，不知道 kbd0 背後是 IRQ1 還是 0x60。
//
// Which devices this machine has, and which of them the console takes input
// from. The shell knows a device's name and kind and nothing else; that IRQ1
// or port 0x60 is behind kbd0 is not something it can see.
bool is_console_input(const device::Device* node) {
    for (std::size_t index = 0; index < console::input_count(); ++index) {
        if (console::input_device(index) == node) return true;
    }
    return false;
}

void command_devices() {
    const auto total = device::count();
    for (std::size_t index = 0; index < total; ++index) {
        const auto* node = device::at(index);
        write("  ");
        write(node->name);
        for (auto column = text::length(node->name); column < 10; ++column) write(" ");
        const char* kind = device::type_name(node->type);
        write(kind);
        if (is_console_input(node)) {
            for (auto column = text::length(kind); column < 8; ++column) write(" ");
            write("console input");
        }
        write("\n");
    }
    write_number(total);
    write_line(total == 1 ? " device" : " devices");
}

// 把一個區塊以十六進位印出來，一行 16 個位元組。用途是直接看磁碟上的東西：
// 檔案系統的標頭長什麼樣、某一塊是不是真的被寫進去了。走的是 open() 加
// block_read()，也就是檔案系統自己會走的那條路。
//
// Print one block in hex, sixteen bytes to a line. It is for looking at what
// is actually on a disk: what a file system's header looks like, whether a
// block really was written. It goes through open() and block_read(), the very
// path a file system takes.
void command_blk() {
    if (argument_count < 3) {
        write_line("blk: needs a device and a block number");
        return;
    }
    const auto descriptor = vfs::open(arguments[1], vfs::OpenFlags::Read, working_directory);
    if (descriptor < 0) {
        write("blk: ");
        write(arguments[1]);
        write(": ");
        write_line(vfs::error_text(descriptor));
        return;
    }
    const auto block_bytes = vfs::block_size(descriptor);
    if (block_bytes == 0) {
        write("blk: ");
        write(arguments[1]);
        write_line(": not a block device");
        vfs::close(descriptor);
        return;
    }
    std::uint64_t block = 0;
    for (const char* digit = arguments[2]; *digit != '\0'; ++digit) {
        if (*digit < '0' || *digit > '9') {
            write_line("blk: the block number has to be a number");
            vfs::close(descriptor);
            return;
        }
        block = block * 10 + static_cast<std::uint64_t>(*digit - '0');
    }

    // 一次印一塊，緩衝區因此不必和裝置的區塊一樣大。超過緩衝區的區塊直接
    // 拒絕，而不是印出半塊讓人以為那就是全部。
    //
    // One block at a time, so the buffer need not match the device's block
    // size. A block larger than the buffer is refused rather than printed by
    // halves, which would look like the whole of it.
    unsigned char buffer[512];
    if (block_bytes > sizeof(buffer)) {
        write_line("blk: this device's blocks are larger than the dump buffer");
        vfs::close(descriptor);
        return;
    }
    const auto result = vfs::block_read(descriptor, block, 1, buffer);
    vfs::close(descriptor);
    if (!result || result.transferred == 0) {
        write_line("blk: read failed (is the block past the end of the device?)");
        return;
    }
    char digits[5];
    for (std::size_t offset = 0; offset < block_bytes; offset += 16) {
        format::to_hex(digits, sizeof(digits), offset, 4);
        write(digits);
        write("  ");
        for (std::size_t index = 0; index < 16 && offset + index < block_bytes; ++index) {
            format::to_hex(digits, sizeof(digits), buffer[offset + index], 2);
            write(digits);
            write(" ");
        }
        write(" ");
        for (std::size_t index = 0; index < 16 && offset + index < block_bytes; ++index) {
            const char value = static_cast<char>(buffer[offset + index]);
            console::write(value >= ' ' && value <= '~' ? &value : ".", 1);
        }
        write("\n");
    }
}

// 從檔案系統讀出一個程式並執行它。這是 VFS 與 ELF loader 接在一起的地方：
// shell 只給一個路徑，載入器只認得那份映像，兩者都不知道檔案是從哪個裝置
// 上讀出來的。
//
// Read a program out of the file system and run it. This is where the VFS and
// the ELF loader meet: the shell hands over a path, the loader sees only the
// image, and neither knows which device the file came off.
void run_program(const char* path) {
    // 執行程式並等它結束。行程現在有收尾機制：exit 系統呼叫會把控制權交回
    // 這裡，因此程式跑完之後提示符會再度出現，結束碼也一併印出來。
    //
    // Run the program and wait for it to finish. A process has teardown now:
    // the exit syscall hands control back here, so the prompt returns after the
    // program is done and its exit status is printed alongside.
    int status = 0;
    if (!user::launch(path, &status)) {
        write(path);
        write_line(": could not be started");
        return;
    }
    write("[");
    write(path);
    write(" exited with status ");
    write_number(static_cast<std::uint64_t>(status) & 0xff);
    write_line("]");
}

void command_exec() {
    if (argument_count < 2) {
        write_line("exec: needs a program to run");
        return;
    }
    // 相對路徑要先解析成絕對路徑：載入器拿到的是路徑，而它不知道 shell 的
    // 工作目錄在哪裡。
    //
    // A relative path is resolved first: the loader receives a path and knows
    // nothing of the shell's working directory.
    char path[vfs::max_path_length];
    if (!vfs::normalize(arguments[1], working_directory, path, sizeof(path))) {
        write_line("exec: that path is too long");
        return;
    }
    run_program(path);
}

void command_clear() {
    // ANSI：清除整個畫面，再把游標移回左上角。
    // ANSI: erase the whole screen, then move the cursor back to the top left.
    write("\x1b[2J\x1b[H");
}

void command_hello() { run_program("/bin/hello"); }

void print_motd() {
    const auto descriptor = vfs::open("/etc/motd");
    if (descriptor < 0) return;
    char buffer[read_chunk];
    for (;;) {
        const auto result = vfs::read(descriptor, buffer, sizeof(buffer));
        if (!result || result.transferred == 0) break;
        console::write(buffer, result.transferred);
    }
    vfs::close(descriptor);
}

void execute() {
    split_line();
    if (argument_count == 0) return;
    const char* command = arguments[0];
    if (text::equals(command, "help")) return command_help();
    if (text::equals(command, "ls")) return command_ls();
    if (text::equals(command, "cat")) return command_cat();
    if (text::equals(command, "cd")) return command_cd();
    if (text::equals(command, "pwd")) return command_pwd();
    if (text::equals(command, "stat")) return command_stat();
    if (text::equals(command, "echo")) return command_echo();
    if (text::equals(command, "mem")) return command_mem();
    if (text::equals(command, "uptime")) return command_uptime();
    if (text::equals(command, "devices")) return command_devices();
    if (text::equals(command, "mount")) return command_mount();
    if (text::equals(command, "blk")) return command_blk();
    if (text::equals(command, "exec")) return command_exec();
    if (text::equals(command, "version")) return command_version();
    if (text::equals(command, "clear")) return command_clear();
    if (text::equals(command, "hello")) return command_hello();
    if (text::equals(command, "reboot")) platform::restart();
    if (text::equals(command, "poweroff")) platform::power_off();
    write(command);
    write_line(": unknown command (try help)");
}

void prompt() {
    write("shirley:");
    write(working_directory);
    write("$ ");
}

} // namespace

[[noreturn]] void run() {
    if (vfs::mounted()) print_motd();
    write_line("");
    if (io::standard_input() == nullptr) {
        // 沒有輸入裝置時提示符只會騙人：永遠不會有字元進來。此時停在等待
        // 中斷的迴圈，讓計時器之類的中斷仍然照常運作。
        //
        // Without an input device a prompt would only lie, because no
        // character can ever arrive. Park in a wait loop instead, so
        // interrupts such as the timer keep being serviced.
        write_line("No input device on this platform; the console is output only.");
        for (;;) arch::wait_for_interrupt();
    }
    for (;;) {
        prompt();
        read_line();
        execute();
    }
}

} // namespace shirley::shell
