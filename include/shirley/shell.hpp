#pragma once

namespace shirley::shell {

// 執行主控台 shell。這是核心啟動流程的最後一步，因此不會返回：沒有可以
// 返回的地方，開機後的機器就是這個提示符。
//
// Run the console shell. It is the last step of kernel start-up and therefore
// never returns: there is nowhere to return to, and after boot this prompt is
// what the machine is.
[[noreturn]] void run();

} // namespace shirley::shell
