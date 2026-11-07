#pragma once

#include <cstdint>
#include <string>

namespace cdmp::session2 {

// 3527 CommonInteractionActorComponent::Serialize calls its condition serializer
// with a null holder for native, stage-less derived interactions. Supply an
// immutable empty holder to that call only; never alter the actor's state.
// Nonnull holders and entries outside the proven native shape are unchanged.
uintptr_t interaction_condition_argument(uintptr_t holder, uintptr_t entry) noexcept;

// Required before opening the shared-field host tunnel on 3527. Legacy 2976
// needs no change. Unsupported builds and mismatched code are refused.
bool interaction_condition_install();
std::string interaction_condition_describe();

}  // namespace cdmp::session2
