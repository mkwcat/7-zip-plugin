// Darch.cpp - File for decoding GameCube/Wii arc/u8 archives
//   Written by mkwcat
//
// This file is part of the mkwcat 7-Zip plugin project.

#include "Darch.hpp"
#include "Util.hpp"

#include <C/CpuArch.h>

#include <CPP/Common/ComTry.h>
#include <CPP/Common/MyBuffer.h>
#include <CPP/Common/MyCom.h>
#include <CPP/Common/UTFConvert.h>

#include <CPP/7zip/Archive/IArchive.h>
#include <CPP/7zip/Common/LimitedStreams.h>
#include <CPP/7zip/Common/ProgressUtils.h>
#include <CPP/7zip/Common/RegisterArc.h>
#include <CPP/7zip/Common/StreamObjects.h>
#include <CPP/7zip/Common/StreamUtils.h>

#include <CPP/7zip/Compress/CopyCoder.h>

struct CItem {
    AString Name;
    int Parent;
    bool IsDir;
    UInt32 Offset;
    UInt32 Size;
};

struct CItemEx : public CItem {
    CByteArr Data;
};

Z7_CLASS_IMP_CHandler_IInArchive_2(IInArchiveGetStream, IOutArchive)
#if CLANG_FORMAT_WORKAROUND
    class CHandler
{
#endif
    CMyComPtr<IInStream> _inStream;
    CObjectVector<CItem> _items;
    CByteArr _metadata;
    UInt32 _rootCount;
    UInt32 _strTabOffset;
    size_t _metadataSize;

    int AddEntry(Byte* entries, int index, int parent, size_t maxSize);
    HRESULT Open2(IInStream* stream);
};

static const Byte kArcProps[] = {
    kpidHeadersSize,
};

static const Byte kProps[] = {
    kpidPath,
    kpidIsDir,
    kpidSize,
};

IMP_IInArchive_Props;
IMP_IInArchive_ArcProps;

static const UInt32 kHeaderSize = 0x20;

int CHandler::AddEntry(Byte* entries, int index, int parent, size_t maxSize)
{
    PRINT("Index %d, parent %d\n", index, parent);

    if (index >= _rootCount) {
        return false;
    }

    size_t offset = index * 0xC;
    if (offset + 0xC > maxSize) {
        return false;
    }

    CItem item;
    item.Parent = parent;
    UInt32 stringOffset =
        _strTabOffset + (GetBe32(entries + offset) & 0x00FFFFFF);
    // Read file name string
    UInt32 pos = stringOffset;
    for (;;) {
        if (pos >= maxSize)
            return false;
        const Byte c = entries[pos];
        if (c == 0)
            break;
        pos++;
    }
    item.Name.SetFrom(
        (const char*) (entries + stringOffset), pos - stringOffset
    );

    PRINT("str: %s\n", (const char*) (entries + stringOffset));

    if (entries[offset] == 0x00) {
        item.IsDir = false;
        item.Offset = GetBe32(entries + offset + 0x4);
        item.Size = GetBe32(entries + offset + 0x8);
        _items.Add(item);
        return index + 1;
    } else if (entries[offset] == 0x01) {
        item.IsDir = true;
        UInt32 subItemEnd = GetBe32(entries + offset + 0x8);
        if (!item.Name.IsEmpty()) {
            parent = _items.Size();
            _items.Add(item);
        }
        int subIndex = index + 1;
        for (; (UInt32) subIndex < subItemEnd;) {
            subIndex = AddEntry(entries, subIndex, parent, maxSize);
            if (subIndex == -1) {
                return -1;
            }
        }
        return subIndex;
    } else {
        return -1;
    }
}

HRESULT CHandler::Open2(IInStream* stream)
{
    Byte buf[kHeaderSize];
    RINOK(ReadStream_FALSE(stream, buf, kHeaderSize))
    if (GetBe32(buf) != 0x55AA382D) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    UInt32 entriesOffset = GetBe32(buf + 4);
    _metadataSize = (size_t) GetBe32(buf + 8);
    if (entriesOffset < kHeaderSize || _metadataSize < 0xC) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    RINOK(InStream_SeekSet(stream, entriesOffset))
    _metadata.Alloc(_metadataSize);
    RINOK(ReadStream_FALSE(stream, &_metadata[0], _metadataSize))

    PRINT("OK %d\n", __LINE__);

    if (_metadata[0] != 0x01) {
        return S_FALSE;
    }

    _rootCount = GetBe32(&_metadata[0x8]);
    _strTabOffset = _rootCount * 0xC;

    if (!AddEntry((Byte*) &_metadata[0], 0, -1, _metadataSize)) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    return S_OK;
}

Z7_COM7F_IMF(CHandler::Open(
    IInStream* stream, const UInt64* /* maxCheckStartPosition */,
    IArchiveOpenCallback* /* openArchiveCallback */
))
{
    PRINT("Open\n");

    COM_TRY_BEGIN
    {
        Close();
        if (Open2(stream) != S_OK) {
            PRINT("Open failure\n");
            return S_FALSE;
        }
        PRINT("Open ok\n");
        _inStream = stream;
    }
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Close())
{
    PRINT("Close\n");

    _inStream.Release();
    _items.Clear();
    _metadata.Free();
    _metadataSize = 0;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32* numItems))
{
    PRINT("GetNumberOfItems\n");

    *numItems = _items.Size();
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT* value))
{
    PRINT("GetArchiveProperty\n");

    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    switch (propID) {
    case kpidHeadersSize:
        prop = kHeaderSize + _metadataSize;
        break;
    case kpidExtension:
        prop = "arc";
        break;
    }
    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

static void
Utf8StringToProp(const AString& s, NWindows::NCOM::CPropVariant& prop)
{
    if (!s.IsEmpty()) {
        UString us;
        ConvertUTF8ToUnicode(s, us);
        prop = us;
    }
}

static void
PropToUtf8String(const NWindows::NCOM::CPropVariant& prop, AString& s)
{
    if (prop.vt == VT_BSTR) {
        ConvertUnicodeToUTF8(prop.bstrVal, s);
    } else {
        s.Empty();
    }
}

Z7_COM7F_IMF(
    CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
)
{
    // PRINT("GetProperty\n");

    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    const CItem& item = _items[index];

    switch (propID) {
    case kpidPath: {
        AString path;
        unsigned cur = index;
        for (;;) {
            const CItem& item2 = _items[cur];
            if (!path.IsEmpty()) {
                path.InsertAtFront(CHAR_PATH_SEPARATOR);
            }
            if (item2.Name.IsEmpty()) {
                path.Insert(0, "unknown");
            } else {
                path.Insert(0, item2.Name);
            }
            cur = (unsigned) item2.Parent;
            if (item2.Parent < 0)
                break;
        }

        PRINT("path: %s\n", &path[0]);
        Utf8StringToProp(path, prop);
        break;
    }

    case kpidIsDir:
        prop = _items[index].IsDir;
        break;

    case kpidSize:
    case kpidPackSize:
        prop = item.Size;
        break;
    }

    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::Extract(
    const UInt32* indices, UInt32 numItems, Int32 testMode,
    IArchiveExtractCallback* extractCallback
))
{
    PRINT("Extract\n");

    COM_TRY_BEGIN
    const bool allFilesMode = (numItems == (UInt32) (Int32) -1);
    if (allFilesMode)
        numItems = _items.Size();
    if (numItems == 0)
        return S_OK;
    UInt64 totalSize = 0;
    UInt32 i;
    for (i = 0; i < numItems; i++) {
        const CItem& item = _items[allFilesMode ? i : indices[i]];
        totalSize += item.Size;
    }
    extractCallback->SetTotal(totalSize);

    UInt64 currentTotalSize = 0;

    NCompress::CCopyCoder* copyCoderSpec = new NCompress::CCopyCoder();
    CMyComPtr<ICompressCoder> copyCoder = copyCoderSpec;

    CLocalProgress* lps = new CLocalProgress;
    CMyComPtr<ICompressProgressInfo> progress = lps;
    lps->Init(extractCallback, false);

    CLimitedSequentialInStream* streamSpec = new CLimitedSequentialInStream;
    CMyComPtr<ISequentialInStream> fileStream(streamSpec);
    streamSpec->SetStream(_inStream);

    for (i = 0; i < numItems; i++) {
        lps->InSize = lps->OutSize = currentTotalSize;
        RINOK(lps->SetCur())
        CMyComPtr<ISequentialOutStream> realOutStream;
        const Int32 askMode = testMode ? NArchive::NExtract::NAskMode::kTest
                                       : NArchive::NExtract::NAskMode::kExtract;
        const UInt32 index = allFilesMode ? i : indices[i];
        const CItem& item = _items[index];
        RINOK(extractCallback->GetStream(index, &realOutStream, askMode))
        currentTotalSize += item.Size;

        if (!testMode && !realOutStream)
            continue;
        RINOK(extractCallback->PrepareOperation(askMode))
        if (testMode) {
            RINOK(extractCallback->SetOperationResult(
                NArchive::NExtract::NOperationResult::kOK
            ))
            continue;
        }
        bool isOk = true;
        RINOK(InStream_SeekSet(_inStream, item.Offset))
        streamSpec->Init(item.Size);
        RINOK(copyCoder->Code(fileStream, realOutStream, NULL, NULL, progress))
        isOk = (copyCoderSpec->TotalSize == item.Size);
        realOutStream.Release();
        RINOK(extractCallback->SetOperationResult(
            isOk ? NArchive::NExtract::NOperationResult::kOK
                 : NArchive::NExtract::NOperationResult::kDataError
        ))
    }

    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetFileTimeType(UInt32* type))
{
    *type = k_PropVar_TimePrec_0;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::UpdateItems(
    ISequentialOutStream* outStream, UInt32 numItems,
    IArchiveUpdateCallback* callback
))
{
    COM_TRY_BEGIN

    PRINT("UpdateItems\n");
    PRINT("numItems: %d\n", numItems);

    if (numItems == 0) {
        return S_FALSE;
    }

    for (UInt32 i = 0; i < numItems; i++) {
        NWindows::NCOM::CPropVariant prop;
        RINOK(callback->GetProperty(i, kpidPath, &prop))
        AString path;
        PropToUtf8String(prop, path);
        PRINT("update path: %s\n", &path[0]);
    }

    return S_FALSE;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream** stream))
{
    PRINT("GetStream\n");

    *stream = NULL;
    COM_TRY_BEGIN

    const CItem& item = _items[index];
    if (item.IsDir == false) {
        return CreateLimitedInStream(_inStream, item.Offset, item.Size, stream);
    }

    return S_FALSE;
    COM_TRY_END
}

static const Byte k_Signature[] = {0x55, 0xAA, 0x38, 0x2D};

REGISTER_ARC_I(
    "darch", "arc u8", NULL, 0xA1, //
    k_Signature, //
    0, //
    NArcInfoFlags::kPreArc, 0
)
