// © 2021 NVIDIA Corporation
#include "NRIFramework.h"
#include "imgui.h"

// STB
#include "stb_image.h"

// ASSIMP
#include <assimp/cimport.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/version.h>
#include <vector>

constexpr uint32_t VIEW_MASK = 0b11;
constexpr nri::Color32f COLOR_0 = { 1.0f, 1.0f, 0.0f, 1.0f };
constexpr nri::Color32f COLOR_1 = { 0.46f, 0.72f, 0.0f, 1.0f };
struct ConstantBufferLayout {
	glm::mat4 modelMat;
	glm::mat4 viewMat;
	glm::mat4 projectMat;
};

struct Vertex {
	vec3 position;
	vec2 uv;
	vec3 normal;
	Vertex(vec3 pos, vec2 uv, vec3 nor) :
			position(pos), uv(uv), normal(nor) {}
};

static uint32_t g_indexCount = 0;

struct NRIInterface : public nri::CoreInterface,
					  public nri::HelperInterface,
					  public nri::StreamerInterface,
					  public nri::SwapChainInterface {};

struct Frame {
	nri::CommandAllocator *commandAllocator;
	nri::CommandBuffer *commandBuffer;
	nri::Descriptor *constantBufferView;
	nri::DescriptorSet *constantBufferDescriptorSet;
	uint64_t constantBufferViewOffset;
};

class Sample : public SampleBase {
public:
	Sample() {}

	~Sample();

	bool Initialize(nri::GraphicsAPI graphicsAPI) override;
	void PrepareFrame(uint32_t frameIndex) override;
	void RenderFrame(uint32_t frameIndex) override;

private:
	NRIInterface NRI = {};
	nri::Device *m_Device = nullptr;
	nri::Streamer *m_Streamer = nullptr;
	nri::SwapChain *m_SwapChain = nullptr;
	nri::Queue *m_GraphicsQueue = nullptr;
	nri::Fence *m_FrameFence = nullptr;
	nri::DescriptorPool *m_DescriptorPool = nullptr;
	nri::PipelineLayout *m_PipelineLayout = nullptr;
	nri::Pipeline *m_Pipeline = nullptr;
	nri::PipelineLayout *m_SkyPipelineLayout = nullptr;
	nri::Pipeline *m_SkyPipeline = nullptr;
	nri::Pipeline *m_PipelineMultiview = nullptr;
	nri::DescriptorSet *m_TextureDescriptorSet = nullptr;
	nri::DescriptorSet *m_SkyTextureDescriptorSet = nullptr;
	nri::Descriptor *m_TextureShaderResource = nullptr;
	nri::Descriptor *m_CubeTextureShaderResource = nullptr;
	nri::Descriptor *m_DepthAttachment = nullptr;
	nri::Descriptor *m_Sampler = nullptr;
	nri::Buffer *m_ConstantBuffer = nullptr;
	nri::Buffer *m_GeometryBuffer = nullptr;
	nri::Texture *m_Texture = nullptr;
	nri::Texture *m_CubeTexture = nullptr;
	nri::Texture *m_DepthTexture = nullptr;

	std::array<Frame, BUFFERED_FRAME_MAX_NUM> m_Frames = {};
	std::vector<BackBuffer> m_SwapChainBuffers;
	std::vector<nri::Memory *> m_MemoryAllocations;

	uint64_t m_GeometryOffset = 0;
	bool m_Multiview = false;
	float m_Transparency = 1.0f;
	float m_Scale = 1.0f;
	float m_Fov = 45.0f;
	vec4 skyParams;
};

Sample::~Sample() {
	NRI.WaitForIdle(*m_GraphicsQueue);

	for (Frame &frame : m_Frames) {
		NRI.DestroyCommandBuffer(*frame.commandBuffer);
		NRI.DestroyCommandAllocator(*frame.commandAllocator);
		NRI.DestroyDescriptor(*frame.constantBufferView);
	}

	for (BackBuffer &backBuffer : m_SwapChainBuffers) {
		NRI.DestroyDescriptor(*backBuffer.colorAttachment);
	}

	NRI.DestroyPipeline(*m_Pipeline);
	NRI.DestroyPipeline(*m_PipelineMultiview);
	NRI.DestroyPipelineLayout(*m_PipelineLayout);
	NRI.DestroyDescriptor(*m_TextureShaderResource);
	NRI.DestroyDescriptor(*m_DepthAttachment);
	NRI.DestroyDescriptor(*m_Sampler);
	NRI.DestroyBuffer(*m_ConstantBuffer);
	NRI.DestroyBuffer(*m_GeometryBuffer);
	NRI.DestroyTexture(*m_Texture);
	NRI.DestroyTexture(*m_DepthTexture);
	NRI.DestroyDescriptorPool(*m_DescriptorPool);
	NRI.DestroyFence(*m_FrameFence);
	NRI.DestroySwapChain(*m_SwapChain);
	NRI.DestroyStreamer(*m_Streamer);

	for (nri::Memory *memory : m_MemoryAllocations) {
		NRI.FreeMemory(*memory);
	}

	DestroyUI(NRI);

	nri::nriDestroyDevice(*m_Device);
}

bool Sample::Initialize(nri::GraphicsAPI graphicsAPI) {
	nri::AdapterDesc bestAdapterDesc = {};
	uint32_t adapterDescsNum = 1;
	NRI_ABORT_ON_FAILURE(
			nri::nriEnumerateAdapters(&bestAdapterDesc, adapterDescsNum));

	// Device
	nri::DeviceCreationDesc deviceCreationDesc = {};
	deviceCreationDesc.graphicsAPI = graphicsAPI;
	deviceCreationDesc.enableGraphicsAPIValidation = true;
	deviceCreationDesc.enableNRIValidation = m_DebugNRI;
	deviceCreationDesc.enableD3D11CommandBufferEmulation =
			D3D11_COMMANDBUFFER_EMULATION;
	deviceCreationDesc.vkBindingOffsets = VK_BINDING_OFFSETS;
	deviceCreationDesc.adapterDesc = &bestAdapterDesc;
	deviceCreationDesc.allocationCallbacks = m_AllocationCallbacks;
	NRI_ABORT_ON_FAILURE(nri::nriCreateDevice(deviceCreationDesc, m_Device));

	// NRI
	NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device,
			NRI_INTERFACE(nri::CoreInterface),
			(nri::CoreInterface *)&NRI));
	NRI_ABORT_ON_FAILURE(nri::nriGetInterface(*m_Device,
			NRI_INTERFACE(nri::HelperInterface),
			(nri::HelperInterface *)&NRI));
	NRI_ABORT_ON_FAILURE(
			nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::StreamerInterface),
					(nri::StreamerInterface *)&NRI));
	NRI_ABORT_ON_FAILURE(
			nri::nriGetInterface(*m_Device, NRI_INTERFACE(nri::SwapChainInterface),
					(nri::SwapChainInterface *)&NRI));

	// Create streamer
	nri::StreamerDesc streamerDesc = {};
	streamerDesc.dynamicBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
	streamerDesc.dynamicBufferUsageBits =
			nri::BufferUsageBits::VERTEX_BUFFER | nri::BufferUsageBits::INDEX_BUFFER;
	streamerDesc.constantBufferMemoryLocation = nri::MemoryLocation::HOST_UPLOAD;
	streamerDesc.frameInFlightNum = BUFFERED_FRAME_MAX_NUM;
	NRI_ABORT_ON_FAILURE(NRI.CreateStreamer(*m_Device, streamerDesc, m_Streamer));

	// Command queue
	NRI_ABORT_ON_FAILURE(
			NRI.GetQueue(*m_Device, nri::QueueType::GRAPHICS, 0, m_GraphicsQueue));

	// Fences
	NRI_ABORT_ON_FAILURE(NRI.CreateFence(*m_Device, 0, m_FrameFence));

	// Swap chain
	nri::Format swapChainFormat;
	{
		nri::SwapChainDesc swapChainDesc = {};
		swapChainDesc.window = GetWindow();
		swapChainDesc.queue = m_GraphicsQueue;
		swapChainDesc.format = nri::SwapChainFormat::BT709_G22_8BIT;
		swapChainDesc.verticalSyncInterval = m_VsyncInterval;
		swapChainDesc.width = (uint16_t)GetWindowResolution().first;
		swapChainDesc.height = (uint16_t)GetWindowResolution().second;
		swapChainDesc.textureNum = SWAP_CHAIN_TEXTURE_NUM;
		NRI_ABORT_ON_FAILURE(
				NRI.CreateSwapChain(*m_Device, swapChainDesc, m_SwapChain));

		uint32_t swapChainTextureNum;
		nri::Texture *const *swapChainTextures =
				NRI.GetSwapChainTextures(*m_SwapChain, swapChainTextureNum);
		swapChainFormat = NRI.GetTextureDesc(*swapChainTextures[0]).format;

		for (uint32_t i = 0; i < swapChainTextureNum; i++) {
			nri::Texture2DViewDesc textureViewDesc = {
				swapChainTextures[i], nri::Texture2DViewType::COLOR_ATTACHMENT,
				swapChainFormat
			};

			nri::Descriptor *colorAttachment;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture2DView(textureViewDesc, colorAttachment));

			BackBuffer backBuffer = { colorAttachment, swapChainTextures[i] };
			m_SwapChainBuffers.push_back(backBuffer);
		}
	}

	// Buffered resources
	for (Frame &frame : m_Frames) {
		NRI_ABORT_ON_FAILURE(
				NRI.CreateCommandAllocator(*m_GraphicsQueue, frame.commandAllocator));
		NRI_ABORT_ON_FAILURE(
				NRI.CreateCommandBuffer(*frame.commandAllocator, frame.commandBuffer));
	}

	// Pipeline
	const nri::DeviceDesc &deviceDesc = NRI.GetDeviceDesc(*m_Device);
	utils::ShaderCodeStorage shaderCodeStorage;
	{
		nri::DescriptorRangeDesc descriptorRangeConstant[1];
		descriptorRangeConstant[0] = { 0, 1, nri::DescriptorType::CONSTANT_BUFFER,
			nri::StageBits::ALL };

		nri::DescriptorRangeDesc descriptorRangeTexture[2];
		descriptorRangeTexture[0] = { 0, 2, nri::DescriptorType::TEXTURE,
			nri::StageBits::FRAGMENT_SHADER };
		descriptorRangeTexture[1] = { 0, 1, nri::DescriptorType::SAMPLER,
			nri::StageBits::FRAGMENT_SHADER };

		nri::DescriptorSetDesc descriptorSetDescs[] = {
			{ 0, descriptorRangeConstant,
					helper::GetCountOf(descriptorRangeConstant) },
			{ 1, descriptorRangeTexture, helper::GetCountOf(descriptorRangeTexture) },
		};

		nri::RootConstantDesc rootConstant = { 1, sizeof(float),
			nri::StageBits::FRAGMENT_SHADER };

		nri::PipelineLayoutDesc pipelineLayoutDesc = {};
		pipelineLayoutDesc.descriptorSetNum =
				helper::GetCountOf(descriptorSetDescs);
		pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
		pipelineLayoutDesc.rootConstantNum = 1;
		pipelineLayoutDesc.rootConstants = &rootConstant;
		pipelineLayoutDesc.shaderStages =
				nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

		NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc,
				m_PipelineLayout));

		nri::VertexStreamDesc vertexStreamDesc = {};
		vertexStreamDesc.bindingSlot = 0;
		vertexStreamDesc.stride = sizeof(Vertex);

		nri::VertexAttributeDesc vertexAttributeDesc[3] = {};
		{
			vertexAttributeDesc[0].format = nri::Format::RGB32_SFLOAT;
			vertexAttributeDesc[0].streamIndex = 0;
			vertexAttributeDesc[0].offset = helper::GetOffsetOf(&Vertex::position);
			vertexAttributeDesc[0].d3d = { "POSITION", 0 };
			vertexAttributeDesc[0].vk.location = { 0 };

			vertexAttributeDesc[1].format = nri::Format::RG32_SFLOAT;
			vertexAttributeDesc[1].streamIndex = 0;
			vertexAttributeDesc[1].offset = helper::GetOffsetOf(&Vertex::uv);
			vertexAttributeDesc[1].d3d = { "TEXCOORD", 0 };
			vertexAttributeDesc[1].vk.location = { 1 };

			vertexAttributeDesc[2].format = nri::Format::RGB32_SFLOAT;
			vertexAttributeDesc[2].streamIndex = 0;
			vertexAttributeDesc[2].offset = helper::GetOffsetOf(&Vertex::normal);
			vertexAttributeDesc[2].d3d = { "NORMAL", 0 };
			vertexAttributeDesc[2].vk.location = { 2 };
		}

		nri::VertexInputDesc vertexInputDesc = {};
		vertexInputDesc.attributes = vertexAttributeDesc;
		vertexInputDesc.attributeNum =
				(uint8_t)helper::GetCountOf(vertexAttributeDesc);
		vertexInputDesc.streams = &vertexStreamDesc;
		vertexInputDesc.streamNum = 1;

		nri::InputAssemblyDesc inputAssemblyDesc = {};
		inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

		nri::RasterizationDesc rasterizationDesc = {};
		rasterizationDesc.fillMode = nri::FillMode::SOLID;
		rasterizationDesc.cullMode = nri::CullMode::NONE;

		nri::ColorAttachmentDesc colorAttachmentDesc = {};
		colorAttachmentDesc.format = swapChainFormat;
		colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
		colorAttachmentDesc.blendEnabled = true;
		colorAttachmentDesc.colorBlend = { nri::BlendFactor::SRC_ALPHA,
			nri::BlendFactor::ONE_MINUS_SRC_ALPHA,
			nri::BlendFunc::ADD };

		nri::DepthAttachmentDesc depthAttachmentDesc = {};
		depthAttachmentDesc.write = true;
		depthAttachmentDesc.compareFunc = nri::CompareFunc::LESS_EQUAL;
		depthAttachmentDesc.boundsTest = false;

		nri::OutputMergerDesc outputMergerDesc = {};
		outputMergerDesc.colors = &colorAttachmentDesc;
		outputMergerDesc.colorNum = 1;
		outputMergerDesc.depth = depthAttachmentDesc;
		outputMergerDesc.depthStencilFormat = nri::Format::D16_UNORM;

		nri::ShaderDesc shaderStages[] = {
			utils::LoadShader(deviceDesc.graphicsAPI,
					"simpleMesh.vs", shaderCodeStorage),
			utils::LoadShader(deviceDesc.graphicsAPI, "simpleMesh.fs",
					shaderCodeStorage),
		};

		nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
		graphicsPipelineDesc.pipelineLayout = m_PipelineLayout;
		graphicsPipelineDesc.vertexInput = &vertexInputDesc;
		graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
		graphicsPipelineDesc.rasterization = rasterizationDesc;
		graphicsPipelineDesc.outputMerger = outputMergerDesc;
		graphicsPipelineDesc.shaders = shaderStages;
		graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

		NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(
				*m_Device, graphicsPipelineDesc, m_Pipeline));
	}

	// SKyBox Pipeline
	{
		nri::DescriptorRangeDesc descriptorRangeConstant[1];
		descriptorRangeConstant[0] = { 0, 1, nri::DescriptorType::CONSTANT_BUFFER,
			nri::StageBits::ALL };

		nri::DescriptorRangeDesc descriptorRangeTexture[2];
		descriptorRangeTexture[0] = { 0, 1, nri::DescriptorType::TEXTURE,
			nri::StageBits::FRAGMENT_SHADER };

		descriptorRangeTexture[1] = { 0, 1, nri::DescriptorType::SAMPLER,
			nri::StageBits::FRAGMENT_SHADER };

		nri::DescriptorSetDesc descriptorSetDescs[] = {
			{ 0, descriptorRangeConstant,
					helper::GetCountOf(descriptorRangeConstant) },
			{ 1, descriptorRangeTexture, helper::GetCountOf(descriptorRangeTexture) },
		};

		nri::RootConstantDesc rootConstant = { 1, sizeof(vec4),
			nri::StageBits::FRAGMENT_SHADER };

		nri::PipelineLayoutDesc pipelineLayoutDesc = {};
		pipelineLayoutDesc.descriptorSetNum =
				helper::GetCountOf(descriptorSetDescs);
		pipelineLayoutDesc.descriptorSets = descriptorSetDescs;
		pipelineLayoutDesc.rootConstants = &rootConstant;
		pipelineLayoutDesc.rootConstantNum = 1;
		pipelineLayoutDesc.shaderStages =
				nri::StageBits::VERTEX_SHADER | nri::StageBits::FRAGMENT_SHADER;

		NRI_ABORT_ON_FAILURE(NRI.CreatePipelineLayout(*m_Device, pipelineLayoutDesc,
				m_SkyPipelineLayout));

		nri::InputAssemblyDesc inputAssemblyDesc = {};
		inputAssemblyDesc.topology = nri::Topology::TRIANGLE_LIST;

		nri::RasterizationDesc rasterizationDesc = {};
		rasterizationDesc.fillMode = nri::FillMode::SOLID;
		rasterizationDesc.cullMode = nri::CullMode::NONE;

		nri::ColorAttachmentDesc colorAttachmentDesc = {};
		colorAttachmentDesc.format = swapChainFormat;
		colorAttachmentDesc.colorWriteMask = nri::ColorWriteBits::RGBA;
		colorAttachmentDesc.blendEnabled = true;
		colorAttachmentDesc.colorBlend = { nri::BlendFactor::SRC_ALPHA,
			nri::BlendFactor::ONE_MINUS_SRC_ALPHA,
			nri::BlendFunc::ADD };

		nri::DepthAttachmentDesc depthAttachmentDesc = {};
		depthAttachmentDesc.write = false;
		depthAttachmentDesc.compareFunc = nri::CompareFunc::ALWAYS;
		depthAttachmentDesc.boundsTest = false;

		nri::OutputMergerDesc outputMergerDesc = {};
		outputMergerDesc.colors = &colorAttachmentDesc;
		outputMergerDesc.colorNum = 1;
		outputMergerDesc.depth = depthAttachmentDesc;
		outputMergerDesc.depthStencilFormat = nri::Format::D16_UNORM;

		nri::ShaderDesc shaderStages[] = {
			utils::LoadShader(deviceDesc.graphicsAPI,
					"skybox.vs", shaderCodeStorage),
			utils::LoadShader(deviceDesc.graphicsAPI, "skybox.fs",
					shaderCodeStorage),
		};

		nri::GraphicsPipelineDesc graphicsPipelineDesc = {};
		graphicsPipelineDesc.pipelineLayout = m_SkyPipelineLayout;
		graphicsPipelineDesc.vertexInput = nullptr;
		graphicsPipelineDesc.inputAssembly = inputAssemblyDesc;
		graphicsPipelineDesc.rasterization = rasterizationDesc;
		graphicsPipelineDesc.outputMerger = outputMergerDesc;
		graphicsPipelineDesc.shaders = shaderStages;
		graphicsPipelineDesc.shaderNum = helper::GetCountOf(shaderStages);

		NRI_ABORT_ON_FAILURE(NRI.CreateGraphicsPipeline(
				*m_Device, graphicsPipelineDesc, m_SkyPipeline));
	}

	{ // Descriptor pool
		nri::DescriptorPoolDesc descriptorPoolDesc = {};
		descriptorPoolDesc.descriptorSetMaxNum = BUFFERED_FRAME_MAX_NUM + 2;
		descriptorPoolDesc.constantBufferMaxNum = BUFFERED_FRAME_MAX_NUM;
		descriptorPoolDesc.textureMaxNum = 20;
		descriptorPoolDesc.samplerMaxNum = 10;

		NRI_ABORT_ON_FAILURE(NRI.CreateDescriptorPool(*m_Device, descriptorPoolDesc,
				m_DescriptorPool));
	}

	// Load Scene Mesh
	const aiScene *scene =
			aiImportFile("data/rubber_duck/scene.gltf",
					aiProcess_Triangulate | aiProcess_MakeLeftHanded);
	if (!scene || !scene->HasMeshes()) {
		printf("Unable to load data/rubber_duck/scene.gltf\n");
		exit(255);
	}

	// Load texture
	utils::Texture texture;
	std::string path =
			utils::GetFullPath("Duck_baseColor.png", utils::DataFolder::TEXTURES);
	if (!utils::LoadTexture(path, texture)) {
		return false;
	}

	utils::Texture cubemapHDRTex;
	path = utils::GetFullPath("piazza_bologni_1k.hdr", utils::DataFolder::TEXTURES);
	if (!utils::LoadTexture(path, cubemapHDRTex)) {
		return false;
	}

	int comp;
	int w, h;
	const float *imgHDR = stbi_loadf(path.c_str(), &w, &h, &comp, 4);
	cubemapHDRTex.width = w;
	cubemapHDRTex.height = h;
	cubemapHDRTex.format = nri::Format::RGBA32_SFLOAT;
	cubemapHDRTex.mipNum = 1;

	// Resources
	const uint32_t constantBufferSize = helper::Align((uint32_t)sizeof(ConstantBufferLayout),
			deviceDesc.constantBufferOffsetAlignment);

	const aiMesh *mesh = scene->mMeshes[0];
	std::vector<Vertex> positions;
	std::vector<uint32_t> indices;
	for (unsigned int i = 0; i != mesh->mNumVertices; i++) {
		const aiVector3D v = mesh->mVertices[i];
		const aiVector3D uv0 = mesh->mTextureCoords[0][i];
		const aiVector3D n = mesh->mNormals[i];
		positions.push_back({ vec3(v.x, v.y, v.z), vec2(uv0.x, uv0.y), vec3(n.x, n.y, n.z) });
	}

	for (unsigned int i = 0; i != mesh->mNumFaces; i++) {
		for (int j = 0; j != 3; j++) {
			indices.push_back(mesh->mFaces[i].mIndices[j]);
		}
	}
	g_indexCount = indices.size();
	const uint64_t indexDataSize = helper::GetByteSizeOf(indices);
	const uint64_t indexDataAlignedSize = helper::Align(indexDataSize, 32);
	const uint64_t vertexDataSize = helper::GetByteSizeOf(positions);

	{
		{ // Read-only texture
			nri::TextureDesc textureDesc = {};
			textureDesc.type = nri::TextureType::TEXTURE_2D;
			textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
			textureDesc.format = texture.GetFormat();
			textureDesc.width = texture.GetWidth();
			textureDesc.height = texture.GetHeight();
			textureDesc.mipNum = texture.GetMipNum();

			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture(*m_Device, textureDesc, m_Texture));
		}

		{
			nri::TextureDesc textureDesc = {};
			textureDesc.type = nri::TextureType::TEXTURE_2D;
			textureDesc.usage = nri::TextureUsageBits::SHADER_RESOURCE;
			textureDesc.format = cubemapHDRTex.format;
			textureDesc.width = cubemapHDRTex.width;
			textureDesc.height = cubemapHDRTex.height;
			textureDesc.mipNum = cubemapHDRTex.mipNum;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture(*m_Device, textureDesc, m_CubeTexture));
		}

		{
			nri::TextureDesc textureDesc = {};
			textureDesc.type = nri::TextureType::TEXTURE_2D;
			textureDesc.usage = nri::TextureUsageBits::DEPTH_STENCIL_ATTACHMENT;
			textureDesc.format = nri::Format::D16_UNORM;
			textureDesc.width = (uint16_t)GetWindowResolution().first;
			textureDesc.height = (uint16_t)GetWindowResolution().second;
			textureDesc.mipNum = 1;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture(*m_Device, textureDesc, m_DepthTexture));
		}

		{
			nri::BufferDesc bufferDesc = {};
			bufferDesc.size = constantBufferSize * BUFFERED_FRAME_MAX_NUM;
			bufferDesc.usage = nri::BufferUsageBits::CONSTANT_BUFFER;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateBuffer(*m_Device, bufferDesc, m_ConstantBuffer));
		}

		{ // Geometry buffer1（duck)
			nri::BufferDesc bufferDesc = {};
			bufferDesc.size = indexDataAlignedSize + vertexDataSize;
			bufferDesc.usage = nri::BufferUsageBits::VERTEX_BUFFER |
					nri::BufferUsageBits::INDEX_BUFFER;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateBuffer(*m_Device, bufferDesc, m_GeometryBuffer));
			m_GeometryOffset = indexDataAlignedSize;
		}
	}

	std::vector<nri::Buffer *> constantBufferArray = { m_ConstantBuffer };

	nri::ResourceGroupDesc resourceGroupDesc = {};
	resourceGroupDesc.memoryLocation = nri::MemoryLocation::HOST_UPLOAD;
	resourceGroupDesc.bufferNum = constantBufferArray.size();
	resourceGroupDesc.buffers = constantBufferArray.data();

	m_MemoryAllocations.resize(1, nullptr);
	NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(*m_Device, resourceGroupDesc,
			m_MemoryAllocations.data()));

	std::vector<nri::Buffer *> bufferArray = { m_GeometryBuffer };
	std::vector<nri::Texture *> textureArray = { m_Texture, m_DepthTexture, m_CubeTexture };
	resourceGroupDesc.memoryLocation = nri::MemoryLocation::DEVICE;
	resourceGroupDesc.bufferNum = bufferArray.size();
	resourceGroupDesc.buffers = bufferArray.data();
	resourceGroupDesc.textureNum = textureArray.size();
	resourceGroupDesc.textures = textureArray.data();

	m_MemoryAllocations.resize(
			1 + NRI.CalculateAllocationNumber(*m_Device, resourceGroupDesc), nullptr);
	NRI_ABORT_ON_FAILURE(NRI.AllocateAndBindMemory(
			*m_Device, resourceGroupDesc, m_MemoryAllocations.data() + 1));

	{ // Descriptors
		{ // Read-only texture
			nri::Texture2DViewDesc texture2DViewDesc = {
				m_Texture, nri::Texture2DViewType::SHADER_RESOURCE_2D,
				texture.GetFormat()
			};
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture2DView(texture2DViewDesc, m_TextureShaderResource));
		}

		{
			nri::Texture2DViewDesc textureViewDesc = { .texture = m_CubeTexture, .viewType = nri::Texture2DViewType::SHADER_RESOURCE_2D, .format = cubemapHDRTex.format };
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture2DView(textureViewDesc, m_CubeTextureShaderResource));
		}

		{
			nri::Texture2DViewDesc textureViewDesc = { .texture = m_DepthTexture, .viewType = nri::Texture2DViewType::DEPTH_STENCIL_ATTACHMENT, .format = nri::Format::D16_UNORM };
			NRI_ABORT_ON_FAILURE(
					NRI.CreateTexture2DView(textureViewDesc, m_DepthAttachment));
		}

		{ // Sampler
			nri::SamplerDesc samplerDesc = {};
			samplerDesc.addressModes = { nri::AddressMode::REPEAT,
				nri::AddressMode::MIRRORED_REPEAT };
			samplerDesc.filters = { nri::Filter::LINEAR, nri::Filter::LINEAR,
				nri::Filter::LINEAR };
			samplerDesc.anisotropy = 4;
			samplerDesc.mipMax = 16.0f;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateSampler(*m_Device, samplerDesc, m_Sampler));
		}

		// Constant buffer
		for (uint32_t i = 0; i < BUFFERED_FRAME_MAX_NUM; i++) {
			nri::BufferViewDesc bufferViewDesc = {};
			bufferViewDesc.buffer = m_ConstantBuffer;
			bufferViewDesc.viewType = nri::BufferViewType::CONSTANT;
			bufferViewDesc.offset = i * constantBufferSize;
			bufferViewDesc.size = constantBufferSize;
			NRI_ABORT_ON_FAILURE(
					NRI.CreateBufferView(bufferViewDesc, m_Frames[i].constantBufferView));

			m_Frames[i].constantBufferViewOffset = bufferViewDesc.offset;
		}
	}

	{ // Descriptor sets
		// Texture
		NRI_ABORT_ON_FAILURE(
				NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 1,
						&m_TextureDescriptorSet, 1, 0));

		std::vector<nri::Descriptor *> shaderResoruceViewArray = { m_TextureShaderResource, m_CubeTextureShaderResource };

		nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
		descriptorRangeUpdateDescs[0].descriptorNum = shaderResoruceViewArray.size();
		descriptorRangeUpdateDescs[0].descriptors = shaderResoruceViewArray.data();

		descriptorRangeUpdateDescs[1].descriptorNum = 1;
		descriptorRangeUpdateDescs[1].descriptors = &m_Sampler;

		NRI.UpdateDescriptorRanges(*m_TextureDescriptorSet, 0,
				helper::GetCountOf(descriptorRangeUpdateDescs),
				descriptorRangeUpdateDescs);

		// Constant buffer
		for (Frame &frame : m_Frames) {
			NRI_ABORT_ON_FAILURE(
					NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_PipelineLayout, 0,
							&frame.constantBufferDescriptorSet, 1, 0));

			nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDesc = {
				&frame.constantBufferView, 1
			};
			NRI.UpdateDescriptorRanges(*frame.constantBufferDescriptorSet, 0, 1,
					&descriptorRangeUpdateDesc);
		}
	}

	// SkyBox Descriptor Sets
	{
		// Texture
		NRI_ABORT_ON_FAILURE(
				NRI.AllocateDescriptorSets(*m_DescriptorPool, *m_SkyPipelineLayout, 1,
						&m_SkyTextureDescriptorSet, 1, 0));

		std::vector<nri::Descriptor *> shaderResoruceViewArray = { m_CubeTextureShaderResource };

		nri::DescriptorRangeUpdateDesc descriptorRangeUpdateDescs[2] = {};
		descriptorRangeUpdateDescs[0].descriptorNum = shaderResoruceViewArray.size();
		descriptorRangeUpdateDescs[0].descriptors = shaderResoruceViewArray.data();

		descriptorRangeUpdateDescs[1].descriptorNum = 1;
		descriptorRangeUpdateDescs[1].descriptors = &m_Sampler;

		NRI.UpdateDescriptorRanges(*m_SkyTextureDescriptorSet, 0,
				helper::GetCountOf(descriptorRangeUpdateDescs),
				descriptorRangeUpdateDescs);
	}

	{ // Upload data
		std::vector<uint8_t> geometryBufferData(indexDataAlignedSize +
				vertexDataSize);
		memcpy(&geometryBufferData[0], indices.data(), indexDataSize);
		memcpy(&geometryBufferData[indexDataAlignedSize], positions.data(),
				vertexDataSize);

		std::array<nri::TextureSubresourceUploadDesc, 16> subresources;
		for (uint32_t mip = 0; mip < texture.GetMipNum(); mip++) {
			texture.GetSubresource(subresources[mip], mip);
		}

		nri::TextureUploadDesc textureData;
		textureData.subresources = subresources.data();
		textureData.texture = m_Texture;
		textureData.after = { nri::AccessBits::SHADER_RESOURCE,
			nri::Layout::SHADER_RESOURCE };
		textureData.planes = nri::PlaneBits::ALL;

		nri::TextureUploadDesc textureData1;
		textureData1.subresources = nullptr;
		textureData1.texture = m_DepthTexture;
		textureData1.after = { nri::AccessBits::DEPTH_STENCIL_ATTACHMENT_WRITE, nri::Layout::DEPTH_STENCIL_ATTACHMENT };
		textureData1.planes = nri::PlaneBits::DEPTH;

		nri::TextureSubresourceUploadDesc cubeSubresources;
		cubeSubresources.slices = imgHDR;
		cubeSubresources.sliceNum = 1;
		cubeSubresources.rowPitch = cubemapHDRTex.width * 16;
		cubeSubresources.slicePitch = cubeSubresources.rowPitch * cubemapHDRTex.height;

		nri::TextureUploadDesc textureData2;
		textureData2.subresources = &cubeSubresources;
		textureData2.texture = m_CubeTexture;
		textureData2.after = { nri::AccessBits::SHADER_RESOURCE, nri::Layout::SHADER_RESOURCE };
		textureData2.planes = nri::PlaneBits::ALL;

		nri::BufferUploadDesc bufferData = {};
		bufferData.buffer = m_GeometryBuffer;
		bufferData.data = &geometryBufferData[0];
		bufferData.dataSize = geometryBufferData.size();
		bufferData.after = { nri::AccessBits::INDEX_BUFFER |
			nri::AccessBits::VERTEX_BUFFER };

		std::vector<nri::BufferUploadDesc> uploadDescArray = { bufferData };
		std::vector<nri::TextureUploadDesc> texUploadDescArray = { textureData, textureData1, textureData2 };

		NRI_ABORT_ON_FAILURE(NRI.UploadData(*m_GraphicsQueue, texUploadDescArray.data(), texUploadDescArray.size(),
				uploadDescArray.data(),
				uploadDescArray.size()));
	}

	// User interface
	bool initialized = InitUI(NRI, NRI, *m_Device, swapChainFormat);

	return initialized;
}

void Sample::PrepareFrame(uint32_t) {
	BeginUI();

	ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(0, 0));
	ImGui::Begin("Settings", nullptr, ImGuiWindowFlags_NoResize);
	{
		ImGui::SliderFloat("Transparency", &m_Transparency, 0.0f, 1.0f);
		ImGui::SliderFloat("Scale", &m_Scale, 0.75f, 1.25f);
		ImGui::SliderFloat("Fov", &m_Fov, 20.0f, 120.0f,"%.0f");

		const nri::DeviceDesc &deviceDesc = NRI.GetDeviceDesc(*m_Device);
		ImGui::BeginDisabled(!deviceDesc.isFlexibleMultiviewSupported);
		ImGui::Checkbox("Multiview", &m_Multiview);
		ImGui::EndDisabled();
	}
	ImGui::End();

	ImGui::ShowDemoWindow();

	EndUI(NRI, *m_Streamer);
	NRI.CopyStreamerUpdateRequests(*m_Streamer);
}

void Sample::RenderFrame(uint32_t frameIndex) {
	nri::Dim_t w = (nri::Dim_t)GetWindowResolution().first;
	nri::Dim_t h = (nri::Dim_t)GetWindowResolution().second;
	nri::Dim_t w2 = w / 2;
	nri::Dim_t h2 = h / 2;

	const uint32_t bufferedFrameIndex = frameIndex % BUFFERED_FRAME_MAX_NUM;
	const Frame &frame = m_Frames[bufferedFrameIndex];

	if (frameIndex >= BUFFERED_FRAME_MAX_NUM) {
		NRI.Wait(*m_FrameFence, 1 + frameIndex - BUFFERED_FRAME_MAX_NUM);
		NRI.ResetCommandAllocator(*frame.commandAllocator);
	}

	const uint32_t currentTextureIndex =
			NRI.AcquireNextSwapChainTexture(*m_SwapChain);
	BackBuffer &currentBackBuffer = m_SwapChainBuffers[currentTextureIndex];

	const glm::mat4 m = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
			glm::vec3(1.f, 0.f, 0.f));
	const glm::mat4 v =
			glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -0.8f, 3.5f)),
					(float)glfwGetTime(), glm::vec3(0.0f, 1.0f, 0.0f));
	const glm::mat4 p = glm::perspectiveLH_ZO(glm::radians(m_Fov), 900.f / 600.f, 0.1f, 100.0f);

	skyParams.x = 0;
	skyParams.y = p[1][1];
	skyParams.z = 0;
	skyParams.w = p[0][0];

	ConstantBufferLayout *commonConstants = (ConstantBufferLayout *)NRI.MapBuffer(
			*m_ConstantBuffer, frame.constantBufferViewOffset,
			sizeof(ConstantBufferLayout));

	if (commonConstants) {
		commonConstants->modelMat = m;
		commonConstants->viewMat = v;
		commonConstants->projectMat = p;
		NRI.UnmapBuffer(*m_ConstantBuffer);
	}

	// Record
	nri::CommandBuffer *commandBuffer = frame.commandBuffer;
	NRI.BeginCommandBuffer(*commandBuffer, m_DescriptorPool);
	{
		nri::TextureBarrierDesc textureBarrierDescs = {};
		textureBarrierDescs.texture = currentBackBuffer.texture;
		textureBarrierDescs.after = { nri::AccessBits::COLOR_ATTACHMENT,
			nri::Layout::COLOR_ATTACHMENT };
		std::array<nri::TextureBarrierDesc, 2> texBarrierArray = { textureBarrierDescs };
		nri::BarrierGroupDesc barrierGroupDesc = {};
		barrierGroupDesc.textureNum = 1;
		barrierGroupDesc.textures = &textureBarrierDescs;

		NRI.CmdBarrier(*commandBuffer, barrierGroupDesc);

		// Single- or multi- view
		nri::AttachmentsDesc attachmentsDesc = {};
		attachmentsDesc.colorNum = 1;
		attachmentsDesc.colors = &currentBackBuffer.colorAttachment;
		attachmentsDesc.depthStencil = m_DepthAttachment;
		attachmentsDesc.viewMask = 0;

		NRI.CmdBeginRendering(*commandBuffer, attachmentsDesc);
		{
			{
				helper::Annotation annotation(NRI, *commandBuffer, "Clears");

				nri::ClearDesc clearDesc = {};
				clearDesc.planes = nri::PlaneBits::COLOR;
				clearDesc.value.color.f = COLOR_0;

				NRI.CmdClearAttachments(*commandBuffer, &clearDesc, 1, nullptr, 0);
				clearDesc = {};
				clearDesc.planes = nri::PlaneBits::DEPTH;
				clearDesc.value.depthStencil.depth = 1.0;
				NRI.CmdClearAttachments(*commandBuffer, &clearDesc, 1, nullptr, 0);
			}

			{
				helper::Annotation annotation(NRI, *commandBuffer, "SkyBox");
				NRI.CmdSetPipelineLayout(*commandBuffer, *m_SkyPipelineLayout);
				NRI.CmdSetPipeline(*commandBuffer, *m_SkyPipeline);
				NRI.CmdSetRootConstants(*commandBuffer, 0, &skyParams, sizeof(vec4));
				NRI.CmdSetDescriptorSet(*commandBuffer, 0,
						*frame.constantBufferDescriptorSet, nullptr);
				NRI.CmdSetDescriptorSet(*commandBuffer, 1, *m_SkyTextureDescriptorSet,
						nullptr);
				{
					const nri::Viewport viewport = { 0.0f, 0.0f, (float)w,
						(float)h, 0.0f, 1.0f };
					NRI.CmdSetViewports(*commandBuffer, &viewport, 1);

					nri::Rect scissor = { 0, 0, w, h };
					NRI.CmdSetScissors(*commandBuffer, &scissor, 1);
				}
				NRI.CmdDraw(*commandBuffer, { 3, 1, 0, 0 });
			}

			{
				helper::Annotation annotation(NRI, *commandBuffer, "SimpleMesh");

				NRI.CmdSetPipelineLayout(*commandBuffer, *m_PipelineLayout);
				NRI.CmdSetPipeline(*commandBuffer, *m_Pipeline);
				NRI.CmdSetRootConstants(*commandBuffer, 0, &m_Transparency, 4);
				NRI.CmdSetIndexBuffer(*commandBuffer, *m_GeometryBuffer, 0,
						nri::IndexType::UINT32);
				NRI.CmdSetVertexBuffers(*commandBuffer, 0, 1, &m_GeometryBuffer,
						&m_GeometryOffset);
				NRI.CmdSetDescriptorSet(*commandBuffer, 0,
						*frame.constantBufferDescriptorSet, nullptr);
				NRI.CmdSetDescriptorSet(*commandBuffer, 1, *m_TextureDescriptorSet,
						nullptr);

				{
					const nri::Viewport viewport = { 0.0f, 0.0f, (float)w,
						(float)h, 0.0f, 1.0f };
					NRI.CmdSetViewports(*commandBuffer, &viewport, 1);

					nri::Rect scissor = { 0, 0, w, h };
					NRI.CmdSetScissors(*commandBuffer, &scissor, 1);
				}

				NRI.CmdDrawIndexed(*commandBuffer, { g_indexCount, 1, 0, 0, 0 });
			}
		}
		NRI.CmdEndRendering(*commandBuffer);

		// Singleview
		attachmentsDesc.viewMask = 0;

		NRI.CmdBeginRendering(*commandBuffer, attachmentsDesc);
		{
			helper::Annotation annotation(NRI, *commandBuffer, "UI");

			RenderUI(NRI, NRI, *m_Streamer, *commandBuffer, 1.0f, true);
		}
		NRI.CmdEndRendering(*commandBuffer);

		textureBarrierDescs.before = textureBarrierDescs.after;
		textureBarrierDescs.after = { nri::AccessBits::UNKNOWN,
			nri::Layout::PRESENT };

		NRI.CmdBarrier(*commandBuffer, barrierGroupDesc);
	}
	NRI.EndCommandBuffer(*commandBuffer);

	{ // Submit
		nri::QueueSubmitDesc queueSubmitDesc = {};
		queueSubmitDesc.commandBuffers = &frame.commandBuffer;
		queueSubmitDesc.commandBufferNum = 1;

		NRI.QueueSubmit(*m_GraphicsQueue, queueSubmitDesc);
	}

	// Present
	NRI.QueuePresent(*m_SwapChain);

	{ // Signaling after "Present" improves D3D11 performance a bit
		nri::FenceSubmitDesc signalFence = {};
		signalFence.fence = m_FrameFence;
		signalFence.value = 1 + frameIndex;

		nri::QueueSubmitDesc queueSubmitDesc = {};
		queueSubmitDesc.signalFences = &signalFence;
		queueSubmitDesc.signalFenceNum = 1;

		NRI.QueueSubmit(*m_GraphicsQueue, queueSubmitDesc);
	}
}

SAMPLE_MAIN(Sample, 0);
