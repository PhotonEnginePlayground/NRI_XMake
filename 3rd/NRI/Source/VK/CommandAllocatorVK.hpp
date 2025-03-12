// © 2021 NVIDIA Corporation

CommandAllocatorVK::~CommandAllocatorVK() {
    if (m_OwnsNativeObjects) {
        const auto& vk = m_Device.GetDispatchTable();
        vk.DestroyCommandPool(m_Device, m_Handle, m_Device.GetVkAllocationCallbacks());
    }
}

Result CommandAllocatorVK::Create(const Queue& queue) {
    const QueueVK& queueImpl = (QueueVK&)queue;

    m_Type = queueImpl.GetType();

    const VkCommandPoolCreateInfo info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, nullptr, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, queueImpl.GetFamilyIndex()};

    const auto& vk = m_Device.GetDispatchTable();
    VkResult result = vk.CreateCommandPool(m_Device, &info, m_Device.GetVkAllocationCallbacks(), &m_Handle);
    RETURN_ON_FAILURE(&m_Device, result == VK_SUCCESS, GetReturnCode(result), "vkCreateCommandPool returned %d", (int32_t)result);

    return Result::SUCCESS;
}

Result CommandAllocatorVK::Create(const CommandAllocatorVKDesc& commandAllocatorDesc) {
    if (!commandAllocatorDesc.vkCommandPool)
        return Result::INVALID_ARGUMENT;

    m_OwnsNativeObjects = false;
    m_Handle = (VkCommandPool)commandAllocatorDesc.vkCommandPool;
    m_Type = commandAllocatorDesc.queueType;

    return Result::SUCCESS;
}

NRI_INLINE void CommandAllocatorVK::SetDebugName(const char* name) {
    m_Device.SetDebugNameToTrivialObject(VK_OBJECT_TYPE_COMMAND_POOL, (uint64_t)m_Handle, name);
}

NRI_INLINE Result CommandAllocatorVK::CreateCommandBuffer(CommandBuffer*& commandBuffer) {
    const VkCommandBufferAllocateInfo info = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, nullptr, m_Handle, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1};

    VkCommandBuffer commandBufferHandle = VK_NULL_HANDLE;

    const auto& vk = m_Device.GetDispatchTable();
    VkResult result = vk.AllocateCommandBuffers(m_Device, &info, &commandBufferHandle);
    RETURN_ON_FAILURE(&m_Device, result == VK_SUCCESS, GetReturnCode(result), "vkAllocateCommandBuffers returned %d", (int32_t)result);

    CommandBufferVK* commandBufferImpl = Allocate<CommandBufferVK>(m_Device.GetAllocationCallbacks(), m_Device);
    commandBufferImpl->Create(m_Handle, commandBufferHandle, m_Type);

    commandBuffer = (CommandBuffer*)commandBufferImpl;

    return Result::SUCCESS;
}

NRI_INLINE void CommandAllocatorVK::Reset() {
    const auto& vk = m_Device.GetDispatchTable();
    VkResult result = vk.ResetCommandPool(m_Device, m_Handle, (VkCommandPoolResetFlags)0);
    RETURN_ON_FAILURE(&m_Device, result == VK_SUCCESS, ReturnVoid(), "vkResetCommandPool returned %d", (int32_t)result);
}
