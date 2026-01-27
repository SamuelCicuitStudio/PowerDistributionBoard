#pragma once

#include <unknwn.h>
#include <winrt/base.h>

namespace winrt::impl {
template <typename Async>
auto wait_for(Async const& async, Windows::Foundation::TimeSpan const& timeout);
}
