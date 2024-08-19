// GFArch.cpp - File for decoding archive files from Good-Feel games
//   Written by mkwcat
//
// This file is part of the mkwcat 7-Zip plugin project.

#include "GFArch.hpp"
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

namespace GFArch
{

enum CompressionType {
    GFCP_NONE = 0,
    GFCP_BPE = 1,
    GFCP_LZ77 = 2,
    GFCP_COMP_COUNT,
};

Z7_CLASS_IMP_CHandler_IInArchive_2(IInArchiveGetStream, IOutArchive)
#if CLANG_FORMAT_WORKAROUND
    class CHandler
{
#endif
    CMyComPtr<IInStream> _inStream;
    CByteArr _metadata;
    size_t _metadataSize;
    size_t _metadataOffset;
    UInt32 _itemCount;
    UInt32 _fileCount;

    size_t _dataSize;
    size_t _dataOffset;
    CompressionType _compressionType;
    UInt32 _decompressedSize;
    UInt32 _compressedSize;

    CObjectVector<AString> _pathTable;

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

static const UInt32 kHeaderSize = 0x1C;
static const UInt32 kGFCPHeaderSize = 0x14;

static UInt32 CalcNameCrc(const char* name)
{
    UInt32 crc = 0;
    for (int i = 0; name[i]; i++) {
        crc = name[i] + crc * 137;
    }
    return crc;
}

HRESULT CHandler::Open2(IInStream* stream)
{
    _metadataSize = 0;
    _metadataOffset = 0;
    _dataSize = 0;
    _dataOffset = 0;
    _itemCount = 0;
    _fileCount = 0;

    Byte buf[kHeaderSize];
    RINOK(ReadStream_FALSE(stream, buf, kHeaderSize))
    if (GetBe32(buf) != 0x47464143) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    _metadataOffset = GetUi32(buf + 0xC);
    _metadataSize = (size_t) GetUi32(buf + 0x10);
    if (_metadataOffset < kHeaderSize || _metadataSize < 0x4) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    RINOK(InStream_SeekSet(stream, _metadataOffset))
    _metadata.Alloc(_metadataSize);
    RINOK(ReadStream_FALSE(stream, &_metadata[0], _metadataSize))

    PRINT("OK %d\n", __LINE__);

    UInt32 count = GetUi32(_metadata);
    if (count == 0) {
        return S_OK;
    }

    if (count * 0x10 + 4 >= _metadataSize) {
        return S_FALSE;
    }

    if (_metadata[_metadataSize - 1] != 0) {
        return S_FALSE;
    }

    _dataOffset = GetUi32(buf + 0x14);
    _dataSize = GetUi32(buf + 0x18);
    if (_dataSize < kGFCPHeaderSize) {
        return S_FALSE;
    }

    PRINT("OK %d\n", __LINE__);

    Byte gfcp[kGFCPHeaderSize];
    RINOK(InStream_SeekSet(stream, _dataOffset))
    RINOK(ReadStream_FALSE(stream, gfcp, kGFCPHeaderSize))

    if (GetBe32(gfcp) != 0x47464350) {
        return S_FALSE;
    }

    _compressionType = (CompressionType) GetUi32(gfcp + 0x8);
    _decompressedSize = GetUi32(gfcp + 0xC);
    _compressedSize = GetUi32(gfcp + 0x10);

    if (_compressionType >= GFCP_COMP_COUNT) {
        return S_FALSE;
    }

    if (_compressedSize > _dataSize - kGFCPHeaderSize) {
        return S_FALSE;
    }

    // Verify item list
    CObjectVector<UInt32> dirStack;
    AString rootName;
    rootName = "";

    for (UInt32 index = 0; index < count; index++) {
        UInt32 offset = index * 0x10 + 4;

        if (offset + 0x10 > _metadataSize) {
            return S_FALSE;
        }

        UInt32 nameHash = GetUi32(_metadata + offset);
        UInt32 nameOffset = GetUi32(_metadata + offset + 4);
        BYTE flags = nameOffset >> 24;
        nameOffset &= 0x00FFFFFF;

        if (nameOffset < _metadataOffset ||
            nameOffset >= _metadataOffset + _metadataSize) {
            return S_FALSE;
        }

        const char* name =
            (const char*) &_metadata[nameOffset - _metadataOffset];
        if (name[0] == 0) {
            return S_FALSE;
        }

        if (CalcNameCrc(name) != nameHash) {
            PRINT("Name hash mismatch\n");
            // return S_FALSE;
        }

        AString name2;
        name2 = rootName;
        name2 += name;
        _pathTable.Add(name2);

        PRINT("Name: %s\n", &name2[0]);

        if (flags & 0x01) {
            // Directory
            dirStack.Add(index);
        } else {
            _fileCount++;
        }

        if (flags & 0x80 && index != count - 1) {
            bool found = false;
            for (int i = (int) dirStack.Size() - 1; i >= 0; i--) {
                if (GetUi32(_metadata + dirStack[i] * 0x10 + 4 + 0xC) ==
                    _metadataOffset + offset + 0x10) {
                    found = true;
                    rootName = _pathTable[dirStack[i]];
                    rootName += CHAR_PATH_SEPARATOR;

                    dirStack.Delete(i);
                    break;
                }
            }

            if (!found) {
                PRINT("Parent not found\n");
                return S_FALSE;
            }
        }

        _itemCount++;
    }

    if (dirStack.Size() != 0) {
        PRINT("Dir stack not empty\n");
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
    _metadata.Free();
    _pathTable.Clear();
    _metadataSize = 0;
    return S_OK;
}

Z7_COM7F_IMF(CHandler::GetNumberOfItems(UInt32* numItems))
{
    PRINT("GetNumberOfItems\n");

    *numItems = _itemCount;
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
        prop = "gfa";
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

Z7_COM7F_IMF(
    CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
)
{
    // PRINT("GetProperty\n");

    COM_TRY_BEGIN
    NWindows::NCOM::CPropVariant prop;
    if (index >= _itemCount) {
        return S_FALSE;
    }

    UInt32 offset = index * 0x10 + 4;

    switch (propID) {
    case kpidPath: {
        AString path = _pathTable[index];
        PRINT("Path: %s\n", &path[0]);
        Utf8StringToProp(path, prop);
        break;
    }

    case kpidIsDir:
        prop = (GetUi32(_metadata + offset + 4) & 0x01000000) != 0;
        break;

    case kpidSize:
    case kpidPackSize:
        prop = GetUi32(_metadata + offset + 8);
        break;
    }

    prop.Detach(value);
    return S_OK;
    COM_TRY_END
}

struct OutputListEntry {
    size_t offset;
    size_t size;
    UInt32 index;
    bool skip;
    bool open;
    CMyComPtr<ISequentialOutStream> outStream;
};

#define LOGTHING PRINT("Skip: %d\n", outputs[0].skip);

/* Decompress data from input to output */
static HRESULT CopyDecodeBPE(
    ISequentialInStream* inStream, UInt32 decompSize, OutputListEntry* outputs,
    size_t outputCount, IArchiveExtractCallback* extractCallback,
    ICompressProgressInfo* progress, UInt32 askMode
)
{
    LOGTHING

    unsigned char left[256], right[256], stack[256];
    short int c, count, i, size;

    UInt64 totalSize = 0;

    auto getByte = [&]() -> int {
        unsigned char b;
        if (inStream->Read(&b, 1, NULL) != S_OK) {
            return EOF;
        }
        return b;
    };

    class COperation
    {
    public:
        COperation(IArchiveExtractCallback* extractCallback)
        {
            _extractCallback = extractCallback;
            _result = extractCallback->PrepareOperation(
                NArchive::NExtract::NAskMode::kExtract
            );
            if (_result == S_OK) {
                _hasOperation = true;
            }
        }

        ~COperation()
        {
            if (_result == S_OK && _hasOperation) {
                _result = _extractCallback->SetOperationResult(
                    NArchive::NExtract::NOperationResult::kDataError
                );
            }
        }

        HRESULT PrepareOperation(UInt32 askMode)
        {
            if (_result != S_OK) {
                return _result;
            }
            if (_hasOperation) {
                RINOK(EndOperation(NArchive::NExtract::NOperationResult::kOK));
            }
            _result = _extractCallback->PrepareOperation(askMode);
            if (_result == S_OK) {
                _hasOperation = true;
            }
            return _result;
        }

        HRESULT EndOperation(int result)
        {
            if (_result == S_OK) {
                _result = _extractCallback->SetOperationResult(result);
                if (_result == S_OK) {
                    _hasOperation = false;
                }
            }
            return _result;
        }

        IArchiveExtractCallback* _extractCallback;
        bool _hasOperation = false;
        HRESULT _result;
    };

    COperation operation(extractCallback);
    RINOK(operation._result)

    /* Unpack each block until end of file */
    while ((count = getByte()) != EOF) {

        /* Set left to itself as literal flag */
        for (i = 0; i < 256; i++)
            left[i] = i;

        /* Read pair table */
        for (c = 0;;) {

            /* Skip range of literal bytes */
            if (count > 127) {
                c += count - 127;
                count = 0;
            }
            if (c == 256)
                break;

            if (c > 256) {
                PRINT("Invalid byte %d %lld\n", __LINE__, totalSize);
                return S_FALSE;
            }

            /* Read pairs, skip right if literal */
            for (i = 0; i <= count; i++, c++) {
                int n = getByte();
                if (n == EOF) {
                    PRINT("Unexpected EOF\n");
                    return S_FALSE;
                }

                left[c] = n;
                if (c != left[c]) {
                    n = getByte();
                    if (n == EOF) {
                        PRINT("Unexpected EOF\n");
                        return S_FALSE;
                    }

                    right[c] = n;
                }
            }
            if (c == 256)
                break;
            if (c > 256) {
                PRINT("Invalid byte %d\n", __LINE__);
                return S_FALSE;
            }
            count = getByte();
            if (count == EOF) {
                PRINT("Unexpected EOF\n");
                return S_FALSE;
            }
        }

        /* Calculate packed data block size */
        int n = getByte();
        if (n == EOF) {
            PRINT("Unexpected EOF\n");
            return S_FALSE;
        }
        size = 256 * n;
        n = getByte();
        if (n == EOF) {
            PRINT("Unexpected EOF\n");
            return S_FALSE;
        }
        size += n;

        /* Unpack data block */
        for (i = 0;;) {

            /* Pop byte from stack or read byte */
            if (i > 0)
                c = stack[--i];
            else {
                if (!size--)
                    break;
                c = getByte();
                if (c == EOF) {
                    PRINT("Unexpected EOF\n");
                    return S_FALSE;
                }
            }

            /* Output byte or push pair on stack */
            if (c >= 256) {
                PRINT("Invalid byte %d\n", __LINE__);
                return S_FALSE;
            }
            if (c == left[c]) {
                for (size_t o = 0; o < outputCount; o++) {
                    if (totalSize >= outputs[o].offset &&
                        totalSize < outputs[o].offset + outputs[o].size) {
                        if (!outputs[o].open) {
                            // End current operation to start a new one
                            PRINT("End operation\n");
                            RINOK(operation.EndOperation(
                                NArchive::NExtract::NOperationResult::kOK
                            ))

                            RINOK(extractCallback->GetStream(
                                outputs[o].index, &outputs[o].outStream,
                                outputs[o].skip
                                    ? NArchive::NExtract::NAskMode::kSkip
                                    : NArchive::NExtract::NAskMode::kExtract
                            ))
                            outputs[o].open = true;
                            auto mode =
                                outputs[o].outStream
                                    ? askMode
                                    : NArchive::NExtract::NAskMode::kSkip;
                            RINOK(operation.PrepareOperation(mode))
                            if (mode == NArchive::NExtract::NAskMode::kSkip) {
                                PRINT(
                                    "Skip operation %u %d\n", outputs[o].index,
                                    (int) outputs[o].skip
                                );
                            } else {
                                PRINT(
                                    "New operation %u %d\n", outputs[o].index,
                                    (int) outputs[o].skip
                                );
                            }
                        }

                        if (outputs[o].outStream) {
                            char data = c;
                            outputs[o].outStream->Write(&data, 1, NULL);
                        }
                    }

                    if (outputs[o].open &&
                        totalSize + 1 >= outputs[o].offset + outputs[o].size) {
                        // Open but not in range, so close it
                        PRINT("End operation for close\n");

                        if (outputs[o].outStream) {
                            outputs[o].outStream.Release();
                        }

                        RINOK(operation.EndOperation(
                            NArchive::NExtract::NOperationResult::kOK
                        ))

                        outputs[o].open = false;
                        outputs[o].outStream = nullptr;
                    }
                }

                totalSize++;
                if (totalSize >= decompSize) {
                    return S_OK;
                }

                if (progress && (totalSize & (((UInt32) 1 << 8) - 1)) == 0) {
                    RINOK(progress->SetRatioInfo(&totalSize, &totalSize))
                }
            } else {
                if (c >= 256 || i + 2 >= 256) {
                    PRINT("Invalid byte %d\n", __LINE__);
                    return S_FALSE;
                }

                stack[i++] = right[c];
                stack[i++] = left[c];
            }
        }
    }

    PRINT("Unexpected EOF\n");
    return S_FALSE;
}

Z7_COM7F_IMF(CHandler::Extract(
    const UInt32* indices, UInt32 numItems, Int32 testMode,
    IArchiveExtractCallback* extractCallback
))
{
    PRINT("Extract\n");

    if (_compressionType == GFCP_NONE) {
        return S_FALSE;
    }

    COM_TRY_BEGIN
    OutputListEntry* outputs = new OutputListEntry[_itemCount];
    for (UInt32 i = 0; i < _itemCount; i++) {
        Byte* offset = _metadata + i * 0x10 + 0x4;
        if (GetUi32(offset + 0x4) & 0x01000000) {
            // Skip directories
            outputs[i] = {
                .offset = 0,
                .size = 0,
                .index = i,
                .skip = true,
                .open = false,
                .outStream = nullptr,
            };
            continue;
        }

        outputs[i] = {
            .offset = GetUi32(offset + 0xC) - _dataOffset,
            .size = GetUi32(offset + 0x8),
            .index = i,
            .skip = true,
            .open = false,
            .outStream = nullptr,
        };

        PRINT("File %d: %zu %zu\n", i, outputs[i].offset, outputs[i].size);
    }

    LOGTHING

    const bool allFilesMode = (numItems == (UInt32) (Int32) -1);
    if (allFilesMode)
        numItems = _itemCount;
    UInt64 totalSize = 0;
    UInt32 realItemCount = 0;
    UInt32 i;
    for (i = 0; i < numItems; i++) {
        const UInt32 index = allFilesMode ? i : indices[i];
        PRINT("Extract Index: %d\n", index);
        if (index >= _itemCount) {
            return S_FALSE;
        }

        outputs[index].skip = false;

        Byte* offset = _metadata + index * 0x10 + 4;
        if (GetUi32(offset + 4) & 0x01000000) {
            // Skip directories
            continue;
        }

        if (outputs[index].offset + outputs[index].size > totalSize) {
            totalSize = outputs[index].offset + outputs[index].size;
        }

        realItemCount++;
    }

    LOGTHING

    extractCallback->SetTotal(totalSize);
    if (totalSize > _decompressedSize) {
        PRINT("Invalid total size\n");
        return S_FALSE;
    }

    CLocalProgress* lps = new CLocalProgress;
    CMyComPtr<ICompressProgressInfo> progress = lps;
    lps->Init(extractCallback, false);

    CLimitedSequentialInStream* streamSpec = new CLimitedSequentialInStream;
    CMyComPtr<ISequentialInStream> fileStream(streamSpec);
    streamSpec->SetStream(_inStream);

    const Int32 askMode = testMode ? NArchive::NExtract::NAskMode::kTest
                                   : NArchive::NExtract::NAskMode::kExtract;

    RINOK(InStream_SeekSet(_inStream, _dataOffset + kGFCPHeaderSize));
    streamSpec->Init(_decompressedSize);

    PRINT("Decompressing %llu bytes\n", totalSize);
    bool isOk = CopyDecodeBPE(
                    fileStream, totalSize, outputs, _itemCount, extractCallback,
                    progress, askMode
                ) == S_OK;
    PRINT("Decompressing done: %d\n", isOk);

    for (i = 0; i < realItemCount; i++) {
        if (outputs[i].outStream) {
            outputs[i].outStream.Release();
        }
    }

    delete[] outputs;

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

    return S_OK;
    COM_TRY_END
}

Z7_COM7F_IMF(CHandler::GetStream(UInt32 index, ISequentialInStream** stream))
{
    PRINT("GetStream\n");

    *stream = NULL;
    COM_TRY_BEGIN

    return S_FALSE;
    COM_TRY_END
}

static const Byte k_Signature[] = {0x47, 0x46, 0x41, 0x43};

REGISTER_ARC_I(
    "gfarch", "gfa", NULL, 0xA2, //
    k_Signature, //
    0, //
    NArcInfoFlags::kPreArc, 0
)

} // namespace GFArch