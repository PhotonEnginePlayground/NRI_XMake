// © 2021 NVIDIA Corporation

#pragma once
namespace helper {
template <typename T, typename A>
constexpr T Align(T x, A alignment) {
    return (T)((size_t(x) + (size_t)alignment - 1) & ~((size_t)alignment - 1));
}

template <typename T, uint32_t N>
constexpr uint32_t GetCountOf(T const (&)[N]) {
    return N;
}

template <typename T>
constexpr uint32_t GetCountOf(const std::vector<T>& v) {
    return (uint32_t)v.size();
}

template <typename T, size_t N>
constexpr uint32_t GetCountOf(const std::array<T, N>& v) {
    return (uint32_t)v.size();
}

template <typename T, typename U>
constexpr uint32_t GetOffsetOf(U T::*member) {
    return (uint32_t)((char*)&((T*)nullptr->*member) - (char*)nullptr);
}

template <typename T>
constexpr size_t GetByteSizeOf(const std::vector<T>& v) {
    return v.size() * sizeof(decltype(v.back()));
}

struct Annotation {
    const nri::CoreInterface& m_NRI;
    nri::CommandBuffer& m_CommandBuffer;

    inline Annotation(const nri::CoreInterface& NRI, nri::CommandBuffer& commandBuffer, const char* name)
        : m_NRI(NRI), m_CommandBuffer(commandBuffer) {
        m_NRI.CmdBeginAnnotation(m_CommandBuffer, name, nri::BGRA_UNUSED);
    }

    inline ~Annotation() {
        m_NRI.CmdEndAnnotation(m_CommandBuffer);
    }
};
} // namespace helper
