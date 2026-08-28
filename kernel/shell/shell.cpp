#include "shirley/shell.hpp"

#include "shirley/arch.hpp"
#include "shirley/console.hpp"
#include "shirley/format.hpp"
#include "shirley/fs.hpp"
#include "shirley/io.hpp"
#include "shirley/memory.hpp"
#include "shirley/platform.hpp"
#include "shirley/text.hpp"
#include "shirley/user_loader.hpp"

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
char working_directory[fs::max_path_length] = "/";

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

// 取得目前工作目錄對應的項目。工作目錄字串是 cd 從實際項目組出來的，因此
// 正常情況下一定找得到；找不到就代表檔案系統沒有掛載。
//
// Fetch the entry the working directory names. The string was assembled by cd
// from a real entry, so it resolves under all normal circumstances; failing to
// means the file system is not mounted.
bool current_directory(fs::Node& node) { return fs::lookup(working_directory, node); }

// 解析使用者輸入的路徑：絕對路徑從根目錄開始，其餘從工作目錄開始。
// Resolve a path the user typed: an absolute one from the root, anything else
// from the working directory.
bool resolve(const char* path, fs::Node& node) {
    fs::Node base;
    if (!current_directory(base)) return false;
    return fs::lookup(path, node, &base);
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
    write_line("  echo [text ...]   print the arguments");
    write_line("  mem               page allocator totals");
    write_line("  uptime            time since boot, counted in timer interrupts");
    write_line("  version           kernel, architecture, and platform");
    write_line("  clear             clear the screen");
    write_line("  hello             run the embedded user program");
    write_line("  reboot            restart the machine");
    write_line("  poweroff          shut the machine down");
}

void list_entry(const fs::Node& node) {
    if (node.directory) {
        write_padded("<dir>", 8);
    } else {
        write_number_padded(node.size, 8);
    }
    write("  ");
    write(node.name);
    if (node.directory) write("/");
    write("\n");
}

void command_ls() {
    const char* path = argument_count > 1 ? arguments[1] : ".";
    fs::Node node;
    if (!resolve(path, node)) {
        report_missing("ls", path);
        return;
    }
    // 指向檔案的 ls 就是列出那一個檔案，和列出目錄用同一種格式。
    // An ls naming a file lists that one file, in the same format a directory
    // listing uses.
    if (!node.directory) {
        list_entry(node);
        return;
    }
    fs::Node child;
    std::size_t position = 0;
    while (fs::list(node, position, child)) {
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
        fs::Node node;
        if (!resolve(path, node)) {
            report_missing("cat", path);
            continue;
        }
        if (node.directory) {
            write("cat: ");
            write(path);
            write_line(": is a directory");
            continue;
        }
        char buffer[read_chunk];
        std::uint64_t offset = 0;
        while (offset < node.size) {
            const auto result = fs::read(node, offset, buffer, sizeof(buffer));
            if (!result || result.transferred == 0) {
                write("cat: ");
                write(path);
                write_line(": read failed");
                break;
            }
            console::write(buffer, result.transferred);
            offset += result.transferred;
        }
    }
}

void command_cd() {
    const char* path = argument_count > 1 ? arguments[1] : "/";
    fs::Node node;
    if (!resolve(path, node)) {
        report_missing("cd", path);
        return;
    }
    if (!node.directory) {
        write("cd: ");
        write(path);
        write_line(": not a directory");
        return;
    }
    // 工作目錄一律由項目本身組回絕對路徑，因此提示符顯示的永遠是正規化過
    // 的路徑，而不是使用者打進來的那串 "../.." 。
    //
    // The working directory is always rebuilt from the entry itself, so the
    // prompt shows a normalized path rather than the "../.." the user typed.
    char resolved[fs::max_path_length];
    if (!fs::path_of(node, resolved, sizeof(resolved))) {
        write_line("cd: path is too long to represent");
        return;
    }
    text::copy(working_directory, sizeof(working_directory), resolved);
}

void command_pwd() { write_line(working_directory); }

void command_stat() {
    if (argument_count < 2) {
        write_line("stat: needs a path");
        return;
    }
    fs::Node node;
    if (!resolve(arguments[1], node)) {
        report_missing("stat", arguments[1]);
        return;
    }
    char path[fs::max_path_length];
    if (fs::path_of(node, path, sizeof(path))) {
        write("  path    ");
        write_line(path);
    }
    write("  type    ");
    write_line(node.directory ? "directory" : "file");
    if (node.directory) {
        write("  entries ");
        write_number(fs::child_count(node));
        write("\n");
    } else {
        write("  size    ");
        write_number(node.size);
        write_line(" bytes");
    }
    write("  entry   ");
    write_number(node.index);
    write(" of ");
    write_number(fs::entry_count());
    write("\n");
}

void command_echo() {
    for (std::size_t index = 1; index < argument_count; ++index) {
        if (index > 1) write(" ");
        write(arguments[index]);
    }
    write("\n");
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

void command_clear() {
    // ANSI：清除整個畫面，再把游標移回左上角。
    // ANSI: erase the whole screen, then move the cursor back to the top left.
    write("\x1b[2J\x1b[H");
}

void command_hello() {
    // user 程式目前沒有行程收尾機制，離開後回不到 shell，因此在交出控制權
    // 之前把這件事講清楚。
    //
    // A user program has no teardown yet and cannot come back to the shell, so
    // this says as much before handing control over.
    write_line("Running the embedded user program. It takes over the CPU:");
    write_line("the shell does not come back until the machine restarts.");
    if (!user::launch_embedded()) write_line("hello: the embedded user image failed to load");
}

void print_motd() {
    fs::Node node;
    if (!fs::lookup("/etc/motd", node) || node.directory) return;
    char buffer[read_chunk];
    std::uint64_t offset = 0;
    while (offset < node.size) {
        const auto result = fs::read(node, offset, buffer, sizeof(buffer));
        if (!result || result.transferred == 0) return;
        console::write(buffer, result.transferred);
        offset += result.transferred;
    }
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
    if (fs::mounted()) print_motd();
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
