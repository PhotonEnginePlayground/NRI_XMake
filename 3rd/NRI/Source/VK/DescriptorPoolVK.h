// © 2021 NVIDIA Corporation

#pragma once

namespace nri {

struct DescriptorSetVK;

struct DescriptorPoolVK final : public DebugNameBase {
    inline DescriptorPoolVK(DeviceVK& device)
        : m_Device(device)
        , m_AllocatedSets(device.GetStdAllocator()) {
        m_AllocatedSets.reserve(64);
    }

    inline operator VkDescriptorPool() const {
        return m_Handle;
    }

    inline DeviceVK& GetDevice() const {
        return m_Device;
    }

    ~DescriptorPoolVK();

    Result Create(const DescriptorPoolDesc& descriptorPoolDesc);
    Result Create(const DescriptorPoolVKDesc& descriptorPoolVKDesc);

    //================================================================================================================
    // DebugNameBase
    //================================================================================================================

    void SetDebugName(const char* name) DEBUG_NAME_OVERRIDE;

    //================================================================================================================
    // NRI
    //================================================================================================================

    void Reset();
    Result AllocateDescriptorSets(const PipelineLayout& pipelineLayout, uint32_t setIndex, DescriptorSet** descriptorSets, uint32_t instanceNum, uint32_t variableDescriptorNum);

private:
    DeviceVK& m_Device;
    Vector<DescriptorSetVK*> m_AllocatedSets;
    VkDescriptorPool m_Handle = VK_NULL_HANDLE;
    uint32_t m_UsedSets = 0;
    bool m_OwnsNativeObjects = true;
};

} // namespace nri