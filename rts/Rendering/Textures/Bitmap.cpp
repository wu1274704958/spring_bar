/* This file is part of the Spring engine (GPL v2 or later), see LICENSE.html */

#include <algorithm>
#include <utility>
#include <cstring>
#include <cassert>
#include <typeindex>


#include <SDL_video.h>

#ifndef BITMAP_NO_OPENGL
	#include "Rendering/GL/myGL.h"
	#include "System/TimeProfiler.h"
#endif

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_GIF
#ifdef _DEBUG
	#define STBI_FAILURE_USERMSG
#else
	#define STBI_NO_FAILURE_STRINGS
#endif

#include <stb/stb_image.h>

#include "Bitmap.h"
#include "Rendering/GlobalRendering.h"
#include "Rendering/GL/myGL.h"
#include "System/bitops.h"
#include "System/ContainerUtil.h"
#include "System/SafeUtil.h"
#include "System/Log/ILog.h"
#include "System/Threading/ThreadPool.h"
#include "System/FileSystem/DataDirsAccess.h"
#include "System/FileSystem/FileQueryFlags.h"
#include "System/FileSystem/FileHandler.h"
#include "System/FileSystem/FileSystem.h"
#include "System/Threading/SpringThreading.h"
#include "System/SpringMath.h"
#include "System/ScopedResource.h"

#define ENABLE_TEXMEMPOOL 1

struct TexMemPool {
private:
	// (index, size)
	typedef std::pair<size_t, size_t> FreePair;

	std::vector<uint8_t> memArray;
	std::vector<FreePair> freeList;

	// libIL is not thread-safe, neither are {Alloc,Free}
	spring::mutex bmpMutex;

	size_t numAllocs = 0;
	size_t allocSize = 0;
	size_t numFrees  = 0;
	size_t freeSize  = 0;

public:
	void GrabLock() { bmpMutex.lock(); }
	void FreeLock() { bmpMutex.unlock(); }

	spring::mutex& GetMutex() { return bmpMutex; }

	size_t Size() const { return (memArray.size()); }
	size_t AllocIdx(size_t size) { return (Alloc(size) - Base()); }
	size_t AllocIdxRaw(size_t size) { return (AllocRaw(size) - Base()); }

	uint8_t* Base() { return (memArray.data()); }
	uint8_t* Alloc(size_t size) {
		std::lock_guard<spring::mutex> lck(bmpMutex);
		return (AllocRaw(size));
	}

	uint8_t* AllocRaw(size_t size) {
		#if (ENABLE_TEXMEMPOOL == 0)
		uint8_t* mem = new uint8_t[size];
		#else
		uint8_t* mem = nullptr;

		size_t bestPair = size_t(-1);
		size_t bestSize = size_t(-1);
		size_t diffSize = size_t(-1);

		for (bool runDefrag: {true, false}) {
			bestPair = size_t(-1);
			bestSize = size_t(-1);
			diffSize = size_t(-1);

			// find chunk with smallest size difference
			for (size_t i = 0, n = freeList.size(); i < n; i++) {
				if (freeList[i].second < size)
					continue;

				if ((freeList[i].second - size) > diffSize)
					continue;

				bestSize = freeList[bestPair = i].second;
				diffSize = std::min(bestSize - size, diffSize);
			}

			if (bestPair == size_t(-1)) {
				if (runDefrag && DefragRaw())
					continue;

				// give up
				LOG_L(L_ERROR, "[TexMemPool::%s] failed to allocate bitmap of size " _STPF_ "u from pool of total size " _STPF_ "u", __func__, size, Size());
				throw std::bad_alloc();
				return nullptr;
			}

			break;
		}

		mem = &memArray[freeList[bestPair].first];

		if (bestSize > size) {
			freeList[bestPair].first += size;
			freeList[bestPair].second -= size;
		} else {
			// exact match, erase
			freeList[bestPair] = freeList.back();
			freeList.pop_back();
		}

		numAllocs += 1;
		allocSize += size;

		#endif
		return mem;
	}


	void Free(uint8_t* mem, size_t size) {
		std::lock_guard<spring::mutex> lck(bmpMutex);
		FreeRaw(mem, size);
	}

	void FreeRaw(uint8_t* mem, size_t size) {
		#if (ENABLE_TEXMEMPOOL == 0)
		delete[] mem;
		#else
		if (mem == nullptr)
			return;

		assert(size != 0);
		memset(mem, 0, size);
		freeList.emplace_back(mem - Base(), size);

		#if 0
		{
			// check if freed mem overlaps any existing chunks
			const FreePair& p = freeList.back();

			for (size_t i = 0, n = freeList.size() - 1; i < n; i++) {
				const FreePair& c = freeList[i];

				assert(!((p.first < c.first) && (p.first + p.second) > c.first));
				assert(!((c.first < p.first) && (c.first + c.second) > p.first));
			}
		}
		#endif

		numFrees += 1;
		freeSize += size;
		allocSize -= size;

		// most bitmaps are transient, so keep the list short
		// longer-lived textures should be allocated ASAP s.t.
		// rest of the pool remains unfragmented
		// TODO: split into power-of-two subpools?
		if (freeList.size() >= 64 || freeSize >= (memArray.size() >> 4))
			DefragRaw();
		#endif
	}


	void Dispose() {
		freeList = {};
		memArray = {};

		numAllocs = 0;
		allocSize = 0;
		numFrees  = 0;
		freeSize  = 0;
	}
	void Resize(size_t size) {
		std::lock_guard<spring::mutex> lck(bmpMutex);

		if (memArray.empty()) {
			freeList.reserve(32);
			freeList.emplace_back(0, size);

			memArray.resize(size, 0);
		} else {
			assert(size > Size());

			freeList.emplace_back(Size(), size - Size());
			memArray.resize(size, 0);
		}

		LOG_L(L_INFO, "[TexMemPool::%s] poolSize=" _STPF_ "u allocSize=" _STPF_ "u texCount=" _STPF_ "u", __func__, size, allocSize, numAllocs - numFrees);
	}


	bool Defrag() {
		if (freeList.empty())
			return false;

		std::lock_guard<spring::mutex> lck(bmpMutex);
		return (DefragRaw());
	}

	bool DefragRaw() {
		const auto sortPred = [](const FreePair& a, const FreePair& b) { return (a.first < b.first); };
		const auto accuPred = [](const FreePair& a, const FreePair& b) { return FreePair{0, a.second + b.second}; };

		std::sort(freeList.begin(), freeList.end(), sortPred);

		// merge adjacent chunks
		for (size_t i = 0, n = freeList.size(); i < n; /**/) {
			FreePair& currPair = freeList[i++];

			for (size_t j = i; j < n; j++) {
				FreePair& nextPair = freeList[j];

				assert(!((currPair.first + currPair.second) > nextPair.first));

				if ((currPair.first + currPair.second) != nextPair.first)
					break;

				currPair.second += nextPair.second;
				nextPair.second = 0;

				i += 1;
			}
		}


		size_t i = 0;
		size_t j = 0;

		// cleanup zero-length chunks
		while (i < freeList.size()) {
			freeList[j] = freeList[i];

			j += (freeList[i].second != 0);
			i += 1;
		}

		if (j >= freeList.size())
			return false;

		// shrink
		freeList.resize(j);

		const auto freeBeg  = freeList.begin();
		const auto freeEnd  = freeList.end();
		      auto freePair = FreePair{0, 0};

		freePair = std::accumulate(freeBeg, freeEnd, freePair, accuPred);
		freeSize = freePair.second;
		return true;
	}
};

static TexMemPool texMemPool;


static constexpr float blurkernel[9] = {
	1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f,
	2.0f/16.0f, 4.0f/16.0f, 2.0f/16.0f,
	1.0f/16.0f, 2.0f/16.0f, 1.0f/16.0f
};
// this is a minimal list of file formats that (should) be available at all platforms
static constexpr const char* extList[] = {
	"bmp", "tga", "dds", "png", "jpg", "jpeg", "psd", "hdr", "pic"
};

static bool IsValidImageExt(const char* ext)
{
	return std::find_if(std::cbegin(extList), std::cend(extList), [ext](const char* extV) { return strcmp(ext, extV) == 0; }) != std::cend(extList);
}


//////////////////////////////////////////////////////////////////////
// BitmapAction
//////////////////////////////////////////////////////////////////////

class BitmapAction {
public:
	BitmapAction() = delete;
	BitmapAction(CBitmap* bmp_)
		: bmp{ bmp_ }
	{}

	BitmapAction(const BitmapAction& ba) = delete;
	BitmapAction(BitmapAction&& ba) noexcept = delete;

	BitmapAction& operator=(const BitmapAction& ba) = delete;
	BitmapAction& operator=(BitmapAction&& ba) noexcept = delete;

	void SwapBmp(std::unique_ptr<BitmapAction>& baPtr) noexcept {
		std::swap(bmp, baPtr->bmp);
	}

	virtual uint32_t GetPixelSize() const = 0;
	virtual uint32_t GetPixelChannelSize() const = 0;

	virtual void Renormalize(const float3& newCol) = 0;
	virtual void Blur(int iterations = 1, float weight = 1.0f) = 0;
	virtual void Fill(const SColor& c) = 0;

	virtual void InvertColors() = 0;
	virtual void InvertAlpha() = 0;

	static std::unique_ptr<BitmapAction> GetBitmapAction(CBitmap* bmp);
protected:
	CBitmap* bmp;
};

template <typename T, size_t N>
class myarray {
public:
	using A = myarray<T, N>;

	myarray() {
		std::fill(std::begin(storage), std::end(storage), T{});
	}
	myarray(std::initializer_list<T> l) {
		if (l.size() == 1)
			std::fill(std::begin(storage), std::end(storage), *l.begin());
		else {
			assert(l.size() == N);
			std::copy(l.begin(), l.end(), std::begin(storage));
		}
	}
	myarray(T&& l) {
		std::fill(std::begin(storage), std::end(storage), std::forward<T>(l));
	}

	myarray(const myarray& rhs) : myarray() { *this = rhs; }
	myarray(myarray&& rhs) noexcept { *this = std::move(rhs); }
	myarray& operator=(const myarray& rhs) {
		std::copy(rhs.cbegin(), rhs.cend(), begin());
		return *this;
	}
	myarray& operator=(myarray&& rhs) noexcept {
		std::swap(storage, rhs.storage);
		return *this;
	}

	A operator * (const A& f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] *= f[n];
		}
		return a;
	}
	A operator / (const A& f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] /= f[n];
		}
		return a;
	}
	A operator + (const A& f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] += f[n];
		}
		return a;
	}
	A operator - (const A& f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] -= f[n];
		}
		return a;
	}

	A operator * (const T f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] *= f;
		}
		return a;
	}
	A operator / (const T f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] /= f;
		}
		return a;
	}
	A operator + (const T f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] += f;
		}
		return a;
	}
	A operator - (const T f) const {
		A a = *this;
		for (size_t n = 0; n < size(); ++n) {
			a.storage[n] -= f;
		}
		return a;
	}

	A& operator *= (const A& f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] *= f[n];
		}
		return *this;
	}
	A& operator /= (const A& f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] /= f[n];
		}
		return *this;
	}
	A& operator += (const A& f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] += f[n];
		}
		return *this;
	}
	A& operator -= (const A& f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] -= f[n];
		}
		return *this;
	}

	A& operator *= (const T f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] *= f;
		}
		return *this;
	}
	A& operator /= (const T f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] /= f;
		}
		return *this;
	}
	A& operator += (const T f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] += f;
		}
		return *this;
	}
	A& operator -= (const T f) {
		for (size_t n = 0; n < size(); ++n) {
			storage[n] -= f;
		}
		return *this;
	}

	      T& operator[](size_t idx)       { assert(idx >= 0 && idx < N); return storage[idx]; }
	const T& operator[](size_t idx) const { assert(idx >= 0 && idx < N); return storage[idx]; }

	constexpr size_t size() const { return N * sizeof(T); }
	constexpr auto begin()  { return std::begin(storage); }
	constexpr auto end()    { return std::end(storage);   }
	constexpr auto cbegin() const { return std::cbegin(storage); }
	constexpr auto cend()   const { return std::cend(storage); }
	constexpr auto rbegin() { return std::rbegin(storage); }
	constexpr auto rend()   { return std::rend(storage); }
	constexpr auto rbegin() const { return std::crbegin(storage); }
	constexpr auto end()    const { return std::crend(storage); }
protected:
	T storage[N];
};

template<typename T, uint32_t ch>
class TBitmapAction : public BitmapAction {
public:
	using CT = T;
	using PT = myarray<T, ch>;

	using AccumCT = typename std::conditional<std::is_same_v<T, float>, float, uint32_t>::type;

	using CTR = uint8_t[sizeof(T) *  1];
	using PTR = uint8_t[sizeof(T) * ch];
public:
	TBitmapAction() = delete;
	TBitmapAction(CBitmap* bmp_)
		: BitmapAction(bmp_)
	{}

	constexpr CT GetMaxLDRValue() const {
		if constexpr (std::is_same_v<T, float>) {
			return 1.0f;
		}
		else {
			return std::numeric_limits<T>::max();
		}
	}

	PT& GetRef(uint32_t xyOffset) {
		auto* mem = bmp->GetRawMem();
		assert(mem && xyOffset >= 0 && xyOffset <= bmp->GetMemSize() - sizeof(PTR));
		//return *static_cast<PT*>(static_cast<PTR*>(mem[xyOffset]));
		return *(reinterpret_cast<PT*>(&mem[xyOffset]));
	}

	CT& GetRef(uint32_t xyOffset, uint32_t chan) {
		assert(chan > 0 && chan < 4);
		return GetRef(xyOffset)[chan];
	}

	uint32_t GetPixelSize() const override {
		return sizeof(PT);
	}

	uint32_t GetPixelChannelSize() const override {
		return sizeof(CT);
	}

	//CBitmap CanvasResize(const int newx, const int newy, const bool center = true) const;
	//CBitmap CreateRescaled(int newx, int newy) const;
	void Renormalize(const float3& newCol) override;
	void Blur(int iterations = 1, float weight = 1.0f) override;
	void Fill(const SColor& c) override;
	void InvertColors() override;
	void InvertAlpha() override;
};

//fugly way to make CH compile time constant
#define GET_BITMAP_ACTION_HELPER(CH) do { \
	if (bmp->channels == CH) { \
		switch (bmp->dataType) { \
			case GL_FLOAT         : { \
				return std::make_unique<TBitmapAction<float   , CH>>(bmp); \
			} break; \
			case GL_UNSIGNED_SHORT: { \
				return std::make_unique<TBitmapAction<uint16_t, CH>>(bmp); \
			} break; \
			case GL_UNSIGNED_BYTE : { \
				return std::make_unique<TBitmapAction<uint8_t , CH>>(bmp); \
			} break; \
		} \
	} \
} while (0)

std::unique_ptr<BitmapAction> BitmapAction::GetBitmapAction(CBitmap* bmp)
{
	GET_BITMAP_ACTION_HELPER(4);
	GET_BITMAP_ACTION_HELPER(3);
	GET_BITMAP_ACTION_HELPER(2);
	GET_BITMAP_ACTION_HELPER(1);

	assert(false);
	return nullptr;
}

#undef GET_BITMAP_ACTION_HELPER


//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CBitmap::~CBitmap()
{
	texMemPool.Free(GetRawMem(), GetMemSize());
}

CBitmap::CBitmap()
	: xsize(0)
	, ysize(0)
	, channels(4)
	, dataType(0x1401)
	, compressed(false)
	, bitmapAction(nullptr)
{
	CreateBitmapAction();
}

CBitmap::CBitmap(const uint8_t* data, int _xsize, int _ysize, int _channels, uint32_t _dataType)
	: xsize(_xsize)
	, ysize(_ysize)
	, channels(_channels)
	, dataType(_dataType)
	, compressed(false)
	, bitmapAction(nullptr)
{
	CreateBitmapAction();
	assert(GetMemSize() > 0);
	memIdx = texMemPool.AllocIdx(GetMemSize());

	if (data != nullptr) {
		assert(!((GetRawMem() < data) && (GetRawMem() + GetMemSize()) > data));
		assert(!((data < GetRawMem()) && (data + GetMemSize()) > GetRawMem()));

		std::memcpy(GetRawMem(), data, GetMemSize());
	} else {
		std::memset(GetRawMem(), 0, GetMemSize());
	}
}


CBitmap& CBitmap::operator=(const CBitmap& bmp)
{
	if (this != &bmp) {
		// NB: Free preserves size for asserts
		texMemPool.Free(GetRawMem(), GetMemSize());

		if (bmp.GetRawMem() != nullptr) {
			assert(!bmp.compressed);
			assert(bmp.GetMemSize() != 0);

			assert(!((    GetRawMem() < bmp.GetRawMem()) && (    GetRawMem() +     GetMemSize()) > bmp.GetRawMem()));
			assert(!((bmp.GetRawMem() <     GetRawMem()) && (bmp.GetRawMem() + bmp.GetMemSize()) >     GetRawMem()));

			memIdx = texMemPool.AllocIdx(bmp.GetMemSize());

			std::memcpy(GetRawMem(), bmp.GetRawMem(), bmp.GetMemSize());
		} else {
			memIdx = size_t(-1);
		}

		xsize = bmp.xsize;
		ysize = bmp.ysize;
		channels = bmp.channels;
		dataType = bmp.dataType;

		compressed = bmp.compressed;

		#ifndef BITMAP_NO_OPENGL
		textype = bmp.textype;

		ddsimage = bmp.ddsimage;
		#endif
		CreateBitmapAction();
	}

	assert(GetMemSize() == bmp.GetMemSize());
	assert((GetRawMem() != nullptr) == (bmp.GetRawMem() != nullptr));
	return *this;
}

CBitmap& CBitmap::operator=(CBitmap&& bmp) noexcept
{
	if (this != &bmp) {
		std::swap(memIdx, bmp.memIdx);
		std::swap(xsize, bmp.xsize);
		std::swap(ysize, bmp.ysize);
		std::swap(channels, bmp.channels);
		std::swap(dataType, bmp.dataType);
		std::swap(bitmapAction, bmp.bitmapAction);
		bitmapAction->SwapBmp(bmp.bitmapAction);
		std::swap(compressed, bmp.compressed);

		#ifndef BITMAP_NO_OPENGL
		std::swap(textype, bmp.textype);

		std::swap(ddsimage, bmp.ddsimage);
		#endif
	}

	return *this;
}


void CBitmap::InitPool(size_t size)
{
	// only allow expansion; config-size is in MB
	size *= (1024 * 1024);

	if (size > texMemPool.Size())
		texMemPool.Resize(size);

	texMemPool.Defrag();
}


const uint8_t* CBitmap::GetRawMem() const { return ((memIdx == size_t(-1))? nullptr: (texMemPool.Base() + memIdx)); }
      uint8_t* CBitmap::GetRawMem()       { return ((memIdx == size_t(-1))? nullptr: (texMemPool.Base() + memIdx)); }

void CBitmap::CreateBitmapAction()
{
	bitmapAction = BitmapAction::GetBitmapAction(this);
}

int32_t CBitmap::GetIntFmt() const
{
	constexpr uint32_t intFormats[3][5] = {
		{ 0, GL_R8   , GL_RG8  , GL_RGB8  , GL_RGBA8   },
		{ 0, GL_R16  , GL_RG16 , GL_RGB16 , GL_RGBA16  },
		{ 0, GL_R32F , GL_RG32F, GL_RGB32F, GL_RGBA32F }
	};
	switch (dataType) {
	case GL_FLOAT:
		return intFormats[2][channels];
	case GL_UNSIGNED_SHORT:
		return intFormats[1][channels];
	case GL_UNSIGNED_BYTE:
		return intFormats[0][channels];
	default:
		assert(false);
		return 0;
	}
}

int32_t CBitmap::GetExtFmt() const
{
	constexpr uint32_t extFormats[] = { 0, GL_RED, GL_RG , GL_RGB , GL_RGBA }; // GL_R is not accepted for [1]
	return extFormats[channels];
}

size_t CBitmap::GetMemSize() const
{
	return (xsize * ysize * bitmapAction->GetPixelSize());
}

void CBitmap::Alloc(int w, int h, int c, uint32_t dt)
{
	if (!Empty())
		texMemPool.Free(GetRawMem(), GetMemSize());

	xsize = w; ysize = h;
	channels = c; dataType = dt;
	CreateBitmapAction();

	memIdx = texMemPool.AllocIdx(GetMemSize());
	memset(GetRawMem(), 0, GetMemSize());
}

void CBitmap::AllocDummy(const SColor fill)
{
	compressed = false;

	Alloc(1, 1, sizeof(SColor));
	Fill(fill);
}

bool CBitmap::Load(std::string const& filename, float defaultAlpha, int32_t reqNumChannel, uint32_t reqDataType)
{
	bool isValid = false;

	const std::string ext = FileSystem::GetExtension(filename);
	if (!IsValidImageExt(ext.c_str()))
		return false;

	// LHS is only true for "image.dds", "IMAGE.DDS" would be loaded by IL
	// which does not vertically flip DDS images by default, unlike nv_dds
	// most Spring games do not seem to store DDS buildpics pre-flipped so
	// files ending in ".DDS" would appear upside-down if loaded by nv_dds
	//

	const bool loadDDS = (ext == "dds"); // always lower-case
	const bool flipDDS = (filename.find("unitpics") == std::string::npos); // keep buildpics as-is

	const size_t curMemSize = GetMemSize();


	channels = 4;
	#ifndef BITMAP_NO_OPENGL
	textype = GL_TEXTURE_2D;
	#endif

	#define BITMAP_USE_NV_DDS
	#ifdef BITMAP_USE_NV_DDS
	if (loadDDS) {
		#ifndef BITMAP_NO_OPENGL
		compressed = true;
		xsize = 0;
		ysize = 0;
		channels = 0;

		ddsimage.clear();
		if (!ddsimage.load(filename, flipDDS))
			return false;

		xsize = ddsimage.get_width();
		ysize = ddsimage.get_height();
		channels = ddsimage.get_components();
		switch (ddsimage.get_type()) {
			case nv_dds::TextureFlat :
				textype = GL_TEXTURE_2D;
				break;
			case nv_dds::Texture3D :
				textype = GL_TEXTURE_3D;
				break;
			case nv_dds::TextureCubemap :
				textype = GL_TEXTURE_CUBE_MAP;
				break;
			case nv_dds::TextureNone :
			default :
				break;
		}
		return true;
		#else
		// allocate a dummy texture, dds aren't supported in headless
		AllocDummy();
		return true;
		#endif
	}

	compressed = false;
	#else
	compressed = loadDDS;
	#endif


	CFileHandler file(filename);
	std::vector<uint8_t> buffer;

	if (!file.FileExists()) {
		AllocDummy();
		return false;
	}

	if (!file.IsBuffered()) {
		buffer.resize(file.FileSize(), 0);
		file.Read(buffer.data(), buffer.size());
	} else {
		// steal if file was loaded from VFS
		buffer = std::move(file.GetBuffer());
	}

	union DefAlpha {
		uint8_t  vb;
		uint16_t vs;
		float    vf;
		uint8_t  al[4]; //alignment
	} defAlpha;

	{
		std::lock_guard<spring::mutex> lck(texMemPool.GetMutex());

		// do not preserve the image origin since IL does not
		// vertically flip DDS images by default, unlike nv_dds
		stbi_set_flip_vertically_on_load(loadDDS && flipDDS);

		//stbi_info_from_memory(buffer.data(), buffer.size(), &xsize, &ysize, &channels);

		if (reqDataType > 0) {
			assert(reqDataType == GL_FLOAT || reqDataType == GL_UNSIGNED_SHORT || reqDataType == GL_UNSIGNED_BYTE);
			dataType = reqDataType;
		}
		else if (stbi_is_hdr_from_memory(buffer.data(), buffer.size())) {
			dataType = GL_FLOAT;
		}
		else if (stbi_is_16_bit_from_memory(buffer.data(), buffer.size())) {
			dataType = GL_UNSIGNED_SHORT;
		}
		else {
			dataType = GL_UNSIGNED_BYTE;
		}
		CreateBitmapAction();

		assert(reqNumChannel <= 4);

		const auto CopyToBuffer = [this, curMemSize, &reqNumChannel](const auto* imgData) {
			std::swap(channels, reqNumChannel);

			texMemPool.FreeRaw(GetRawMem(), curMemSize);
			memIdx = texMemPool.AllocIdxRaw(GetMemSize());

			std::memset(GetRawMem(), 0x00   , GetMemSize());
			std::memcpy(GetRawMem(), imgData, GetMemSize());
		};

		switch (dataType) {
		case GL_FLOAT: {
			auto scopedImgBuff = spring::ScopedResource(
				stbi_loadf_from_memory(buffer.data(), buffer.size(), &xsize, &ysize, &channels, reqNumChannel),
				stbi_image_free
			);

			if (scopedImgBuff()) {
				isValid = true;
				CopyToBuffer(scopedImgBuff());
				defAlpha.vf = defaultAlpha;
			}
		} break;
		case GL_UNSIGNED_SHORT: {
			auto scopedImgBuff = spring::ScopedResource(
				stbi_load_16_from_memory(buffer.data(), buffer.size(), &xsize, &ysize, &channels, reqNumChannel),
				stbi_image_free
			);

			if (scopedImgBuff()) {
				isValid = true;
				CopyToBuffer(scopedImgBuff());
				defAlpha.vs = static_cast<uint16_t>(defaultAlpha * std::numeric_limits<uint16_t>::max());
			}
		} break;
		case GL_UNSIGNED_BYTE: {
			auto scopedImgBuff = spring::ScopedResource(
				stbi_load_from_memory(buffer.data(), buffer.size(), &xsize, &ysize, &channels, reqNumChannel),
				stbi_image_free
			);

			if (scopedImgBuff()) {
				isValid = true;
				CopyToBuffer(scopedImgBuff());
				defAlpha.vb = static_cast<uint8_t>(defaultAlpha * std::numeric_limits<uint8_t>::max());
			}
		} break;
		default:
			assert(false);
			return false;
		}
	}

	// has to be outside the mutex scope; AllocDummy will acquire it again and
	// LOG can indirectly cause other bitmaps to be loaded through FontTexture
	if (!isValid) {
		LOG_L(L_ERROR, "[BMP::%s] invalid bitmap \"%s\"", __func__, filename.c_str());
		AllocDummy();
		return false;
	}

	// Handle only RGB case
	// reqNumChannel now has original number of channels reported by STB
	if (reqNumChannel == 3) {
		uint8_t* mem = GetRawMem();

		for (int y = 0; y < ysize; ++y) {
			for (int x = 0; x < xsize; ++x) {
				mem += reqNumChannel * bitmapAction->GetPixelChannelSize();
				memcpy(mem, &defAlpha.al[0], bitmapAction->GetPixelChannelSize());
				mem += bitmapAction->GetPixelChannelSize();
			}
		}
	}

	return true;
}

#if 0
bool CBitmap::Save(std::string const& filename, bool opaque, bool logged) const
{
	if (compressed) {
		#ifndef BITMAP_NO_OPENGL
		return ddsimage.save(filename);
		#else
		return false;
		#endif
	}

	if (GetMemSize() == 0)
		return false;


	std::lock_guard<spring::mutex> lck(texMemPool.GetMutex());

	const uint8_t* mem = GetRawMem();
	      uint8_t* buf = texMemPool.AllocRaw(xsize * ysize * 4);

	/* HACK Flip the image so it saves the right way up.
		(Fiddling with ilOriginFunc didn't do anything?)
		Duplicated with ReverseYAxis. */
	for (int y = 0; y < ysize; ++y) {
		for (int x = 0; x < xsize; ++x) {
			const int bi = 4 * (x + (xsize * ((ysize - 1) - y)));
			const int mi = 4 * (x + (xsize * (              y)));

			for (int ch = 0; ch < 3; ++ch) {
				buf[bi + ch] = (ch < channels) ? mem[mi + ch] : 0xFF;
			}

			buf[bi + 3] = (!opaque && channels == 4) ? mem[mi + 3] : 0xFF;
		}
	}

	// clear any previous errors
	while (ilGetError() != IL_NO_ERROR);

	ilOriginFunc(IL_ORIGIN_UPPER_LEFT);
	ilEnable(IL_ORIGIN_SET);

	ilHint(IL_COMPRESSION_HINT, IL_USE_COMPRESSION);
	ilSetInteger(IL_JPG_QUALITY, 80);

	ILuint imageID = 0;
	ilGenImages(1, &imageID);
	ilBindImage(imageID);
	ilTexImage(xsize, ysize, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, buf);
	assert(ilGetError() == IL_NO_ERROR);

	texMemPool.FreeRaw(buf, xsize * ysize * 4);


	const std::string& fsImageExt = FileSystem::GetExtension(filename);
	const std::string& fsFullPath = dataDirsAccess.LocateFile(filename, FileQueryFlags::WRITE);
	const std::wstring& ilFullPath = std::wstring(fsFullPath.begin(), fsFullPath.end());

	bool success = false;

	if (logged)
		LOG("[CBitmap::%s] saving \"%s\" to \"%s\" (IL_VERSION=%d IL_UNICODE=%d)", __func__, filename.c_str(), fsFullPath.c_str(), IL_VERSION, sizeof(ILchar) != 1);

	if (sizeof(void*) >= 4) {
		#if 0
		// NOTE: all Windows buildbot libIL's crash in ilSaveF (!)
		std::vector<ILchar> ilFullPath(fsFullPath.begin(), fsFullPath.end());

		// null-terminate; vectors are not strings
		ilFullPath.push_back(0);

		// IL might be unicode-aware in which case it uses wchar_t{*} strings
		// should not even be necessary because ASCII and UTFx are compatible
		switch (sizeof(ILchar)) {
			case (sizeof( char  )): {                                                                                                     } break;
			case (sizeof(wchar_t)): { std::mbstowcs(reinterpret_cast<wchar_t*>(ilFullPath.data()), fsFullPath.data(), fsFullPath.size()); } break;
			default: { assert(false); } break;
		}
		#endif

		const ILchar* p = (sizeof(ILchar) != 1)?
			reinterpret_cast<const ILchar*>(ilFullPath.data()):
			reinterpret_cast<const ILchar*>(fsFullPath.data());

		switch (int(fsImageExt[0])) {
			case 'b': case 'B': { success = ilSave(IL_BMP, p); } break;
			case 'j': case 'J': { success = ilSave(IL_JPG, p); } break;
			case 'p': case 'P': { success = ilSave(IL_PNG, p); } break;
			case 't': case 'T': { success = ilSave(IL_TGA, p); } break;
			case 'd': case 'D': { success = ilSave(IL_DDS, p); } break;
		}
	} else {
		FILE* file = fopen(fsFullPath.c_str(), "wb");

		if (file != nullptr) {
			switch (int(fsImageExt[0])) {
				case 'b': case 'B': { success = ilSaveF(IL_BMP, file); } break;
				case 'j': case 'J': { success = ilSaveF(IL_JPG, file); } break;
				case 'p': case 'P': { success = ilSaveF(IL_PNG, file); } break;
				case 't': case 'T': { success = ilSaveF(IL_TGA, file); } break;
				case 'd': case 'D': { success = ilSaveF(IL_DDS, file); } break;
			}

			fclose(file);
		}
	}

	if (logged) {
		if (success) {
			LOG("[CBitmap::%s] saved \"%s\" to \"%s\"", __func__, filename.c_str(), fsFullPath.c_str());
		} else {
			LOG("[CBitmap::%s] error 0x%x saving \"%s\" to \"%s\"", __func__, ilGetError(), filename.c_str(), fsFullPath.c_str());
		}
	}

	ilDeleteImages(1, &imageID);
	ilDisable(IL_ORIGIN_SET);
	return success;
}
#else
bool CBitmap::Save(std::string const& filename, bool opaque, bool logged) const {
	return true;
}
#endif

#if 0
bool CBitmap::SaveGrayScale(const std::string& filename) const
{
	if (compressed)
		return false;

	CBitmap bmp = *this;

	for (uint8_t* mem = bmp.GetRawMem(); mem != nullptr; mem = nullptr) {
		// approximate luminance
		bmp.MakeGrayScale();

		// convert RGBA tuples to normalized FLT32 values expected by SaveFloat; GBA are destroyed
		for (int y = 0; y < ysize; ++y) {
			for (int x = 0; x < xsize; ++x) {
				*reinterpret_cast<float*>(&mem[(y * xsize + x) * 4]) = static_cast<float>(mem[(y * xsize + x) * 4 + 0] / 255.0f);
			}
		}

		// save FLT32 data in 16-bit ushort format
		return (bmp.SaveFloat(filename));
	}

	return false;
}
#else
bool CBitmap::SaveGrayScale(const std::string& filename) const {
	return true;
}
#endif

#if 0
bool CBitmap::SaveFloat(std::string const& filename) const
{
	// must have four channels; each RGBA tuple is reinterpreted as a single FLT32 value
	if (GetMemSize() == 0 || channels != 4)
		return false;

	std::lock_guard<spring::mutex> lck(texMemPool.GetMutex());

	// seems IL_ORIGIN_SET only works in ilLoad and not in ilTexImage nor in ilSaveImage
	// so we need to flip the image ourselves
	const uint8_t* u8mem = GetRawMem();
	const float* f32mem = reinterpret_cast<const float*>(&u8mem[0]);

	uint16_t* u16mem = reinterpret_cast<uint16_t*>(texMemPool.AllocRaw(xsize * ysize * sizeof(uint16_t)));

	for (int y = 0; y < ysize; ++y) {
		for (int x = 0; x < xsize; ++x) {
			const int bi = x + (xsize * ((ysize - 1) - y));
			const int mi = x + (xsize * (              y));
			const uint16_t us = f32mem[mi] * 0xFFFF; // convert float 0..1 to ushort
			u16mem[bi] = us;
		}
	}

	ilHint(IL_COMPRESSION_HINT, IL_USE_COMPRESSION);
	ilSetInteger(IL_JPG_QUALITY, 80);

	ILuint imageID = 0;
	ilGenImages(1, &imageID);
	ilBindImage(imageID);
	// note: DevIL only generates a 16bit grayscale PNG when format is IL_UNSIGNED_SHORT!
	//       IL_FLOAT is converted to RGB with 8bit colordepth!
	ilTexImage(xsize, ysize, 1, 1, IL_LUMINANCE, IL_UNSIGNED_SHORT, u16mem);

	texMemPool.FreeRaw(reinterpret_cast<uint8_t*>(u16mem), xsize * ysize * sizeof(uint16_t));


	const std::string& fsImageExt = FileSystem::GetExtension(filename);
	const std::string& fsFullPath = dataDirsAccess.LocateFile(filename, FileQueryFlags::WRITE);

	FILE* file = fopen(fsFullPath.c_str(), "wb");
	bool success = false;

	if (file != nullptr) {
		switch (int(fsImageExt[0])) {
			case 'b': case 'B': { success = ilSaveF(IL_BMP, file); } break;
			case 'j': case 'J': { success = ilSaveF(IL_JPG, file); } break;
			case 'p': case 'P': { success = ilSaveF(IL_PNG, file); } break;
			case 't': case 'T': { success = ilSaveF(IL_TGA, file); } break;
			case 'd': case 'D': { success = ilSaveF(IL_DDS, file); } break;
		}

		fclose(file);
	}

	ilDeleteImages(1, &imageID);
	return success;
}
#else
bool CBitmap::SaveFloat(const std::string& filename) const {
	return true;
}
#endif


#ifndef BITMAP_NO_OPENGL
uint32_t CBitmap::CreateTexture(float aniso, float lodBias, bool mipmaps, uint32_t texID) const
{
	if (compressed)
		return CreateDDSTexture(texID, aniso, lodBias, mipmaps);

	if (GetMemSize() == 0)
		return 0;

	// jcnossen: Some drivers return "2.0" as a version string,
	// but switch to software rendering for non-power-of-two textures.
	// GL_ARB_texture_non_power_of_two indicates that the hardware will actually support it.
	if (!globalRendering->supportNonPowerOfTwoTex && (xsize != next_power_of_2(xsize) || ysize != next_power_of_2(ysize))) {
		CBitmap bm = CreateRescaled(next_power_of_2(xsize), next_power_of_2(ysize));
		return bm.CreateTexture(aniso, mipmaps);
	}

	if (texID == 0)
		glGenTextures(1, &texID);

	glBindTexture(GL_TEXTURE_2D, texID);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	if (lodBias != 0.0f)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
	if (aniso > 0.0f)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);

	if (mipmaps) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glBuildMipmaps(GL_TEXTURE_2D, GetIntFmt(), xsize, ysize, GetExtFmt(), dataType, GetRawMem());
	} else {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, GetIntFmt(), xsize, ysize, 0, GetExtFmt(), dataType, GetRawMem());
	}

	return texID;
}


static void HandleDDSMipmap(GLenum target, bool mipmaps, int num_mipmaps)
{
	if (num_mipmaps > 0) {
		// dds included the MipMaps use them
		glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	} else {
		if (mipmaps && IS_GL_FUNCTION_AVAILABLE(glGenerateMipmap)) {
			// create the mipmaps at runtime
			glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
			glGenerateMipmap(target);
		} else {
			// no mipmaps
			glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		}
	}
}

uint32_t CBitmap::CreateDDSTexture(uint32_t texID, float aniso, float lodBias, bool mipmaps) const
{
	glPushAttrib(GL_TEXTURE_BIT);

	if (texID == 0)
		glGenTextures(1, &texID);

	switch (ddsimage.get_type()) {
		case nv_dds::TextureNone:
			glDeleteTextures(1, &texID);
			texID = 0;
			break;

		case nv_dds::TextureFlat:    // 1D, 2D, and rectangle textures
			glEnable(GL_TEXTURE_2D);
			glBindTexture(GL_TEXTURE_2D, texID);

			if (!ddsimage.upload_texture2D(0, GL_TEXTURE_2D)) {
				glDeleteTextures(1, &texID);
				texID = 0;
				break;
			}

			if (lodBias != 0.0f)
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_LOD_BIAS, lodBias);
			if (aniso > 0.0f)
				glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);

			HandleDDSMipmap(GL_TEXTURE_2D, mipmaps, ddsimage.get_num_mipmaps());
			break;

		case nv_dds::Texture3D:
			glEnable(GL_TEXTURE_3D);
			glBindTexture(GL_TEXTURE_3D, texID);

			if (!ddsimage.upload_texture3D()) {
				glDeleteTextures(1, &texID);
				texID = 0;
				break;
			}

			if (lodBias != 0.0f)
				glTexParameterf(GL_TEXTURE_3D, GL_TEXTURE_LOD_BIAS, lodBias);

			HandleDDSMipmap(GL_TEXTURE_3D, mipmaps, ddsimage.get_num_mipmaps());
			break;

		case nv_dds::TextureCubemap:
			glEnable(GL_TEXTURE_CUBE_MAP);
			glBindTexture(GL_TEXTURE_CUBE_MAP, texID);

			if (!ddsimage.upload_textureCubemap()) {
				glDeleteTextures(1, &texID);
				texID = 0;
				break;
			}

			if (lodBias != 0.0f)
				glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_LOD_BIAS, lodBias);
			if (aniso > 0.0f)
				glTexParameterf(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);

			HandleDDSMipmap(GL_TEXTURE_CUBE_MAP, mipmaps, ddsimage.get_num_mipmaps());
			break;

		default:
			assert(false);
			break;
	}

	glPopAttrib();
	return texID;
}
#else  // !BITMAP_NO_OPENGL

uint32_t CBitmap::CreateTexture(float aniso, float lodBias, bool mipmaps, uint32_t texID) const {
	return 0;
}

uint32_t CBitmap::CreateDDSTexture(uint32_t texID, float aniso, float lodBias, bool mipmaps) const {
	return 0;
}
#endif // !BITMAP_NO_OPENGL


void CBitmap::CreateAlpha(uint8_t red, uint8_t green, uint8_t blue)
{
	uint8_t* mem = GetRawMem();

	float3 aCol;

	for (int a = 0; a < 3; ++a) {
		int cCol = 0;
		int numCounted = 0;

		for (int y = 0; y < ysize; ++y) {
			for (int x = 0; x < xsize; ++x) {
				const int index = (y * xsize + x) * 4;

				if (mem[index + 3] == 0)
					continue;
				if (mem[index + 0] == red && mem[index + 1] == green && mem[index + 2] == blue)
					continue;

				cCol += mem[index + a];
				numCounted += 1;
			}
		}

		if (numCounted != 0)
			aCol[a] = cCol / 255.0f / numCounted;
	}

	const SColor c(red, green, blue);
	const SColor a(aCol.x, aCol.y, aCol.z, 0.0f);
	SetTransparent(c, a);
}


void CBitmap::SetTransparent(const SColor& c, const SColor trans)
{
	if (compressed)
		return;

	constexpr uint32_t RGB = 0x00FFFFFF;

	uint32_t* mem_i = reinterpret_cast<uint32_t*>(GetRawMem());

	for (int y = 0; y < ysize; ++y) {
		for (int x = 0; x < xsize; ++x) {
			if ((*mem_i & RGB) == (c.i & RGB))
				*mem_i = trans.i;

			mem_i++;
		}
	}
}

void CBitmap::Renormalize(const float3& newCol) { bitmapAction->Renormalize(newCol); }
void CBitmap::Blur(int iterations, float weight) { bitmapAction->Blur(iterations, weight); }
void CBitmap::Fill(const SColor& c) { bitmapAction->Fill(c); }
void CBitmap::InvertColors() { bitmapAction->InvertColors(); }
void CBitmap::InvertAlpha() { bitmapAction->InvertAlpha(); }

void CBitmap::CopySubImage(const CBitmap& src, int xpos, int ypos)
{
	if ((xpos + src.xsize) > xsize || (ypos + src.ysize) > ysize) {
		LOG_L(L_WARNING, "CBitmap::CopySubImage src image does not fit into dst!");
		return;
	}

	if (compressed || src.compressed) {
		LOG_L(L_WARNING, "CBitmap::CopySubImage can't copy compressed textures!");
		return;
	}

	const uint8_t* srcMem = src.GetRawMem();
	      uint8_t* dstMem =     GetRawMem();

	for (int y = 0; y < src.ysize; ++y) {
		const int pixelDst = (((ypos + y) *     xsize) + xpos) * bitmapAction->GetPixelSize();
		const int pixelSrc = ((        y  * src.xsize) +    0) * bitmapAction->GetPixelSize();

		// copy the whole line
		std::copy(&srcMem[pixelSrc], &srcMem[pixelSrc] + bitmapAction->GetPixelSize() * src.xsize, &dstMem[pixelDst]);
	}
}


CBitmap CBitmap::CanvasResize(const int newx, const int newy, const bool center) const
{
	CBitmap bm;

	if (xsize > newx || ysize > newy) {
		LOG_L(L_WARNING, "CBitmap::CanvasResize can only upscale (tried to resize %ix%i to %ix%i)!", xsize,ysize,newx,newy);
		bm.AllocDummy();
		return bm;
	}

	const int borderLeft = (center) ? (newx - xsize) / 2 : 0;
	const int borderTop  = (center) ? (newy - ysize) / 2 : 0;

	bm.Alloc(newx, newy, channels);
	bm.CopySubImage(*this, borderLeft, borderTop);

	return bm;
}


SDL_Surface* CBitmap::CreateSDLSurface()
{
	SDL_Surface* surface = nullptr;

	if (channels < 3) {
		LOG_L(L_WARNING, "CBitmap::CreateSDLSurface works only with 24bit RGB and 32bit RGBA pictures!");
		return surface;
	}

	// this will only work with 24bit RGB and 32bit RGBA pictures
	// note: does NOT create a copy of mem, must keep this around
	surface = SDL_CreateRGBSurfaceFrom(GetRawMem(), xsize, ysize, 8 * channels, xsize * channels, 0x000000FF, 0x0000FF00, 0x00FF0000, (channels == 4) ? 0xFF000000 : 0);

	if (surface == nullptr)
		LOG_L(L_WARNING, "CBitmap::CreateSDLSurface Failed!");

	return surface;
}


CBitmap CBitmap::CreateRescaled(int newx, int newy) const
{
	newx = std::max(1, newx);
	newy = std::max(1, newy);

	CBitmap bm;

	if (compressed) {
		LOG_L(L_WARNING, "CBitmap::CreateRescaled doesn't work with compressed textures!");
		bm.AllocDummy();
		return bm;
	}

	if (channels != 4) {
		LOG_L(L_WARNING, "CBitmap::CreateRescaled only works with RGBA data!");
		bm.AllocDummy();
		return bm;
	}

	bm.Alloc(newx, newy);

	const uint8_t* srcMem =    GetRawMem();
	      uint8_t* dstMem = bm.GetRawMem();

	const float dx = (float) xsize / newx;
	const float dy = (float) ysize / newy;

	float cy = 0;
	for (int y = 0; y < newy; ++y) {
		const int sy = (int) cy;
		cy += dy;
		int ey = (int) cy;
		if (ey == sy)
			ey = sy + 1;

		float cx = 0;
		for (int x = 0; x < newx; ++x) {
			const int sx = (int) cx;
			cx += dx;
			int ex = (int) cx;
			if (ex == sx)
				ex = sx + 1;

			int r = 0;
			int g = 0;
			int b = 0;
			int a = 0;

			for (int y2 = sy; y2 < ey; ++y2) {
				for (int x2 = sx; x2 < ex; ++x2) {
					const int index = (y2*xsize + x2) * 4;

					r += srcMem[index + 0];
					g += srcMem[index + 1];
					b += srcMem[index + 2];
					a += srcMem[index + 3];
				}
			}

			const int index = (y*bm.xsize + x) * 4;
			const int denom = (ex - sx) * (ey - sy);

			dstMem[index + 0] = r / denom;
			dstMem[index + 1] = g / denom;
			dstMem[index + 2] = b / denom;
			dstMem[index + 3] = a / denom;
		}
	}

	return bm;
}

void CBitmap::MakeGrayScale()
{
	if (compressed)
		return;

	uint8_t* mem = GetRawMem();

	for (int y = 0; y < ysize; ++y) {
		for (int x = 0; x < xsize; ++x) {
			const int base = ((y * xsize) + x) * 4;
			const float illum =
				(mem[base + 0] * 0.299f) +
				(mem[base + 1] * 0.587f) +
				(mem[base + 2] * 0.114f);
			const uint32_t ival = (uint32_t)(illum * (256.0f / 255.0f));
			const uint8_t  cval = (ival <= 0xFF) ? ival : 0xFF;
			mem[base + 0] = cval;
			mem[base + 1] = cval;
			mem[base + 2] = cval;
		}
	}
}

#if 0
void CBitmap::Tint(const float tint[3])
{
	if (compressed)
		return;

	uint8_t* mem = GetRawMem();

	for (int y = 0; y < ysize; y++) {
		for (int x = 0; x < xsize; x++) {
			const int base = ((y * xsize) + x) * 4;

			mem[base + 0] = TintByte(mem[base + 0], tint[0]);
			mem[base + 1] = TintByte(mem[base + 1], tint[1]);
			mem[base + 2] = TintByte(mem[base + 2], tint[2]);
			// don't touch the alpha channel
		}
	}
}
#else
void CBitmap::Tint(const float tint[3]) {}
#endif


void CBitmap::ReverseYAxis()
{
	if (compressed)
		return; // don't try to flip DDS

	uint8_t* tmp = texMemPool.Alloc(xsize * bitmapAction->GetPixelSize());
	uint8_t* mem = GetRawMem();

	for (int y = 0; y < (ysize / 2); ++y) {
		const int pixelLo = (((y            ) * xsize) + 0) * bitmapAction->GetPixelSize();
		const int pixelHi = (((ysize - 1 - y) * xsize) + 0) * bitmapAction->GetPixelSize();

		// copy the whole line
		std::copy(mem + pixelHi, mem + pixelHi + bitmapAction->GetPixelSize() * xsize, tmp          );
		std::copy(mem + pixelLo, mem + pixelLo + bitmapAction->GetPixelSize() * xsize, mem + pixelHi);
		std::copy(tmp, tmp + bitmapAction->GetPixelSize() * xsize, mem + pixelLo);
	}

	texMemPool.Free(tmp, xsize * bitmapAction->GetPixelSize());
}

template<typename T, uint32_t ch>
void TBitmapAction<T, ch>::Renormalize(const float3& newCol)
{
	if (ch < 3) {
		assert(false);
		return;
	}

	float3 aCol;
	float3 colorDif;

	for (int a = 0; a < 3; ++a) {
		AccumCT cCol = AccumCT{ 0 };
		AccumCT numCounted = AccumCT{ 0 };
		for (int y = 0; y < bmp->ysize; ++y) {
			const uint32_t yOffset = (y * bmp->xsize);
			for (int x = 0; x < bmp->xsize; ++x) {
				auto& pixel = GetRef(yOffset + x);
				if (pixel[3] != CT{ 0 }) {
					cCol += pixel[a];
					numCounted += AccumCT{ 1 };
				}
			}
		}
		aCol[a] = cCol / GetMaxLDRValue() / numCounted;
		//cCol /= xsize*ysize; //??
		colorDif[a] = newCol[a] - aCol[a];
	}

	for (int a = 0; a < 3; ++a) {
		for (int y = 0; y < bmp->ysize; ++y) {
			const uint32_t yOffset = (y * bmp->xsize);
			for (int x = 0; x < bmp->xsize; ++x) {
				auto& pixel = GetRef(yOffset + x);
				const uint32_t index = (y * bmp->xsize + x) * 4;
				float nc = static_cast<float>(pixel[a]) / static_cast<float>(GetMaxLDRValue()) + colorDif[a];
				pixel[a] = static_cast<CT>(std::min(static_cast<float>(GetMaxLDRValue()), std::max(0.0f, nc * static_cast<float>(GetMaxLDRValue()))));
			}
		}
	}
}

template<typename T, uint32_t ch>
void TBitmapAction<T, ch>::Blur(int iterations, float weight)
{
	if (bmp->compressed)
		return;

	CBitmap tmp(nullptr, bmp->xsize, bmp->ysize, bmp->channels, bmp->dataType);

	CBitmap* src =  bmp;
	CBitmap* dst = &tmp;

	for (int i = 0; i < iterations; ++i) {
		auto srcTBA = &static_cast<TBitmapAction<T, ch>&>(*src->bitmapAction);
		auto dstTBA = &static_cast<TBitmapAction<T, ch>&>(*dst->bitmapAction);
		for_mt(0, src->ysize, [src, weight, &srcTBA, &dstTBA](const int y) {
			const uint32_t yBaseOffset = (y * src->xsize);
			for (int x = 0; x < src->xsize; x++) {
				/////////////////////////////////////////

				PT fragment{ 0 };



				for (int i = 0; i < 9; ++i) {
					int yoffset = (i / 3) - 1;
					int xoffset = (i - (yoffset + 1) * 3) - 1;

					const int tx = x + xoffset;
					const int ty = y + yoffset;

					xoffset *= ((tx >= 0) && (tx < src->xsize));
					yoffset *= ((ty >= 0) && (ty < src->ysize));

					const int offset = (yoffset * src->xsize + xoffset);

					if (i == 4)
						fragment += srcTBA->GetRef(yBaseOffset + x + offset) * weight * blurkernel[i];
					else
						fragment += srcTBA->GetRef(yBaseOffset + x + offset)          * blurkernel[i];
				}

				auto& dstPixel = dstTBA->GetRef(yBaseOffset + x);
				for (auto& c : dstPixel) {
					c = std::max(c, CT(0));
				}

				/////////////////////////////////////////
			}
		});

		std::swap(*src, *dst);
	}

	// if dst points to temporary, we are done
	// otherwise need to perform one more swap
	// (e.g. if iterations=1)
	if (dst != bmp)
		return;

	std::swap(*src, *dst);
}

template<typename T, uint32_t ch>
void TBitmapAction<T, ch>::Fill(const SColor& c)
{
	if (bmp->compressed)
		return;

	const float4 fColor = c;
	for (size_t i = 0; i < bmp->xsize * bmp->ysize; i++) {
		auto pix = GetRef(i);
		for (size_t chan = 0; chan < pix.size(); chan) {
			pix[chan] = static_cast<CT>(fColor[chan] * GetMaxLDRValue());
		}
	}
}

template<typename T, uint32_t ch>
void TBitmapAction<T, ch>::InvertColors()
{
	if (bmp->compressed)
		return;

	for (int y = 0; y < bmp->ysize; ++y) {
		const uint32_t yBaseOffset = (y * bmp->xsize);
		for (int x = 0; x < bmp->xsize; ++x) {
			auto& pix = GetRef(yBaseOffset + x);
			//do not invert alpha, thus pix.size() - 1
			for (size_t chan = 0; chan < pix.size() - 1; chan) {
				pix[chan] = GetMaxLDRValue() - std::clamp(pix[chan], CT{ 0 }, GetMaxLDRValue());
			}
		}
	}
}

template<typename T, uint32_t ch>
void TBitmapAction<T, ch>::InvertAlpha()
{
	if (bmp->compressed)
		return;

	for (int y = 0; y < bmp->ysize; ++y) {
		const uint32_t yBaseOffset = (y * bmp->xsize);
		for (int x = 0; x < bmp->xsize; ++x) {
			auto& pix = GetRef(yBaseOffset + x);
			*pix.rbegin() = GetMaxLDRValue() - std::clamp(*pix.rbegin(), CT{ 0 }, GetMaxLDRValue());
		}
	}
}