#include "shirley/boot_protocol.hpp"

namespace shirley::platform {
namespace {

// 交接失敗時使用的空白開機資訊：沒有任何可用記憶體，比誤用韌體殘留資料安全。
// The blank boot information used when the handoff fails: no usable memory at
// all, which is safer than trusting whatever the firmware left behind.
BootInfo empty_boot_info;

} // namespace
} // namespace shirley::platform

// 由 arch/x86_64/entry.S 呼叫。ShirleyOS 的 UEFI 載入器已經完成所有韌體工作，
// 因此這裡不再解析任何韌體格式，只確認收到的交接結構真的來自我們的載入器。
//
// Called from arch/x86_64/entry.S. The ShirleyOS UEFI loader has already done
// all of the firmware work, so nothing is parsed here; this only confirms the
// handoff really came from that loader.
extern "C" const shirley::BootInfo* shirley_platform_boot_info(const void* firmware_table) {
    using namespace shirley;
    const auto* handoff = static_cast<const BootHandoff*>(firmware_table);
    if (!boot_handoff_valid(handoff)) {
        platform::empty_boot_info = {};
        return &platform::empty_boot_info;
    }
    return &handoff->info;
}
