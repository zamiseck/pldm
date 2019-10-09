#include "libpldmresponder/file_io.hpp"
#include "libpldmresponder/file_io_by_type.hpp"
#include "libpldmresponder/file_table.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "libpldm/base.h"
#include "libpldm/file_io.h"

#include <gmock/gmock-matchers.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace pldm::filetable;

class TestFileTable : public testing::Test
{
  public:
    void SetUp() override
    {
        char tmpdir[] = "/tmp/lid.XXXXXX";
        readFilePath = std::string(mkdtemp(tmpdir));
        std::string input("This is a test file for the lid handler");
        std::string readFile(readFilePath);
        readFile += "/80a02023.lid";
        std::ofstream out(readFile.c_str());
        out << input;
        out.close();
        // Create a temporary directory to hold the config file and files to
        // populate the file table.
        char tmppldm[] = "/tmp/pldm_fileio_table.XXXXXX";
        dir = fs::path(mkdtemp(tmppldm));

        // Copy the sample image files to the directory
        fs::copy("./files", dir);

        imageFile = dir / "NVRAM-IMAGE";
        auto jsonObjects = Json::array();
        auto obj = Json::object();
        obj["path"] = imageFile.c_str();
        obj["file_traits"] = 1;

        jsonObjects.push_back(obj);
        obj.clear();
        cksumFile = dir / "NVRAM-IMAGE-CKSUM";
        obj["path"] = cksumFile.c_str();
        obj["file_traits"] = 4;
        jsonObjects.push_back(obj);

        fileTableConfig = dir / "configFile.json";
        std::ofstream file(fileTableConfig.c_str());
        file << std::setw(4) << jsonObjects << std::endl;
    }

    void TearDown() override
    {
        fs::remove_all(dir);
    }

    fs::path dir;
    fs::path imageFile;
    fs::path cksumFile;
    fs::path fileTableConfig;
    std::string readFilePath;

    // <4 bytes - File handle - 0 (0x00 0x00 0x00 0x00)>,
    // <2 bytes - Filename length - 11 (0x0b 0x00>
    // <11 bytes - Filename - ASCII for NVRAM-IMAGE>
    // <4 bytes - File size - 1024 (0x00 0x04 0x00 0x00)>
    // <4 bytes - File traits - 1 (0x01 0x00 0x00 0x00)>
    // <4 bytes - File handle - 1 (0x01 0x00 0x00 0x00)>,
    // <2 bytes - Filename length - 17 (0x11 0x00>
    // <17 bytes - Filename - ASCII for NVRAM-IMAGE-CKSUM>
    // <4 bytes - File size - 16 (0x0f 0x00 0x00 0x00)>
    // <4 bytes - File traits - 4 (0x04 0x00 0x00 0x00)>
    // No pad bytes added since the length for both the file entries in the
    // table is 56, which is a multiple of 4.
    // <4 bytes - Checksum - 2088303182(0x4e 0xfa 0x78 0x7c)>
    Table attrTable = {
        0x00, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x4e, 0x56, 0x52, 0x41, 0x4d, 0x2d,
        0x49, 0x4d, 0x41, 0x47, 0x45, 0x00, 0x04, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x11, 0x00, 0x4e, 0x56, 0x52, 0x41, 0x4d,
        0x2d, 0x49, 0x4d, 0x41, 0x47, 0x45, 0x2d, 0x43, 0x4b, 0x53, 0x55, 0x4d,
        0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x4e, 0xfa, 0x78, 0x7c};
};

namespace pldm
{

namespace responder
{

namespace dma
{

class MockDMA
{
  public:
    MOCK_METHOD5(transferDataHost,
                 int(const fs::path& file, uint32_t offset, uint32_t length,
                     uint64_t address, bool upstream));
};

} // namespace dma
} // namespace responder
} // namespace pldm
using namespace pldm::responder;
using ::testing::_;
using ::testing::Return;

TEST(TransferDataHost, GoodPath)
{
    using namespace pldm::responder::dma;

    MockDMA dmaObj;
    fs::path path("");

    // Minimum length of 16 and expect transferDataHost to be called once
    // returns the default value of 0 (the return type of transferDataHost is
    // int, the default value for int is 0)
    uint32_t length = minSize;
    EXPECT_CALL(dmaObj, transferDataHost(path, 0, length, 0, true)).Times(1);
    auto response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY,
                                         path, 0, length, 0, true, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    ASSERT_EQ(0, memcmp(responsePtr->payload + sizeof(responsePtr->payload[0]),
                        &length, sizeof(length)));

    // maxsize of DMA
    length = maxSize;
    EXPECT_CALL(dmaObj, transferDataHost(path, 0, length, 0, true)).Times(1);
    response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY, path,
                                    0, length, 0, true, 0);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    ASSERT_EQ(0, memcmp(responsePtr->payload + sizeof(responsePtr->payload[0]),
                        &length, sizeof(length)));

    // length greater than maxsize of DMA
    length = maxSize + minSize;
    EXPECT_CALL(dmaObj, transferDataHost(path, 0, maxSize, 0, true)).Times(1);
    EXPECT_CALL(dmaObj, transferDataHost(path, maxSize, minSize, maxSize, true))
        .Times(1);
    response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY, path,
                                    0, length, 0, true, 0);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    ASSERT_EQ(0, memcmp(responsePtr->payload + sizeof(responsePtr->payload[0]),
                        &length, sizeof(length)));

    // length greater than 2*maxsize of DMA
    length = 3 * maxSize;
    EXPECT_CALL(dmaObj, transferDataHost(_, _, _, _, true)).Times(3);
    response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY, path,
                                    0, length, 0, true, 0);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    ASSERT_EQ(0, memcmp(responsePtr->payload + sizeof(responsePtr->payload[0]),
                        &length, sizeof(length)));

    // check for downstream(copy data from host to BMC) parameter
    length = minSize;
    EXPECT_CALL(dmaObj, transferDataHost(path, 0, length, 0, false)).Times(1);
    response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY, path,
                                    0, length, 0, false, 0);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    ASSERT_EQ(0, memcmp(responsePtr->payload + sizeof(responsePtr->payload[0]),
                        &length, sizeof(length)));
}

TEST(TransferDataHost, BadPath)
{
    using namespace pldm::responder::dma;

    MockDMA dmaObj;
    fs::path path("");

    // Minimum length of 16 and transferDataHost returning a negative errno
    uint32_t length = minSize;
    EXPECT_CALL(dmaObj, transferDataHost(_, _, _, _, _)).WillOnce(Return(-1));
    auto response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY,
                                         path, 0, length, 0, true, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR);

    // length greater than maxsize of DMA and transferDataHost returning a
    // negative errno
    length = maxSize + minSize;
    EXPECT_CALL(dmaObj, transferDataHost(_, _, _, _, _)).WillOnce(Return(-1));
    response = transferAll<MockDMA>(&dmaObj, PLDM_READ_FILE_INTO_MEMORY, path,
                                    0, length, 0, true, 0);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR);
}

TEST(ReadFileIntoMemory, BadPath)
{
    uint32_t fileHandle = 0;
    uint32_t offset = 0;
    uint32_t length = 10;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    // Pass invalid payload length
    auto response = readFileIntoMemory(request, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR_INVALID_LENGTH);
}

TEST_F(TestFileTable, ReadFileInvalidFileHandle)
{
    // Invalid file handle in the file table
    uint32_t fileHandle = 2;
    uint32_t offset = 0;
    uint32_t length = 0;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());

    auto response = readFileIntoMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_FILE_HANDLE);
    // Clear the file table contents.
    table.clear();
}

TEST_F(TestFileTable, ReadFileInvalidOffset)
{
    uint32_t fileHandle = 0;
    // The file size is 1024, so the offset is invalid
    uint32_t offset = 1024;
    uint32_t length = 0;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    auto& table = buildFileTable(fileTableConfig.c_str());

    auto response = readFileIntoMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_DATA_OUT_OF_RANGE);
    // Clear the file table contents.
    table.clear();
}

TEST_F(TestFileTable, ReadFileInvalidLength)
{
    uint32_t fileHandle = 0;
    uint32_t offset = 100;
    // Length should be a multiple of dma min size(16)
    uint32_t length = 10;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    auto& table = buildFileTable(fileTableConfig.c_str());

    auto response = readFileIntoMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_READ_LENGTH);
    // Clear the file table contents.
    table.clear();
}

TEST_F(TestFileTable, ReadFileInvalidEffectiveLength)
{
    uint32_t fileHandle = 0;
    // valid offset
    uint32_t offset = 100;
    // length + offset exceeds the size, so effective length is
    // filesize(1024) - offset(100). The effective length is not a multiple of
    // DMA min size(16)
    uint32_t length = 1024;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    auto& table = buildFileTable(fileTableConfig.c_str());

    auto response = readFileIntoMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_READ_LENGTH);
    // Clear the file table contents.
    table.clear();
}

TEST(WriteFileFromMemory, BadPath)
{
    uint32_t fileHandle = 0;
    uint32_t offset = 0;
    uint32_t length = 10;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    // Pass invalid payload length
    auto response = writeFileFromMemory(request, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR_INVALID_LENGTH);

    // The length field is not a multiple of DMA minsize
    response = writeFileFromMemory(request, requestPayloadLength);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_WRITE_LENGTH);
}

TEST_F(TestFileTable, WriteFileInvalidFileHandle)
{
    // Invalid file handle in the file table
    uint32_t fileHandle = 2;
    uint32_t offset = 0;
    uint32_t length = 16;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());

    auto response = writeFileFromMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_FILE_HANDLE);
    // Clear the file table contents.
    table.clear();
}

TEST_F(TestFileTable, WriteFileInvalidOffset)
{
    uint32_t fileHandle = 0;
    // The file size is 1024, so the offset is invalid
    uint32_t offset = 1024;
    uint32_t length = 16;
    uint64_t address = 0;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_RW_FILE_MEM_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    memcpy(request->payload, &fileHandle, sizeof(fileHandle));
    memcpy(request->payload + sizeof(fileHandle), &offset, sizeof(offset));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset), &length,
           sizeof(length));
    memcpy(request->payload + sizeof(fileHandle) + sizeof(offset) +
               sizeof(length),
           &address, sizeof(address));

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(TestFileTable::fileTableConfig.c_str());

    auto response = writeFileFromMemory(request, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_DATA_OUT_OF_RANGE);
    // Clear the file table contents.
    table.clear();
}

TEST(FileTable, ConfigNotExist)
{
    FileTable tableObj("");
    EXPECT_EQ(tableObj.isEmpty(), true);
}

TEST_F(TestFileTable, ValidateFileEntry)
{
    FileTable tableObj(fileTableConfig.c_str());

    // Test file handle 0, the file size is 1K bytes.
    auto value = tableObj.at(0);
    ASSERT_EQ(value.handle, 0);
    ASSERT_EQ(strcmp(value.fsPath.c_str(), imageFile.c_str()), 0);
    ASSERT_EQ(static_cast<uint32_t>(fs::file_size(value.fsPath)), 1024);
    ASSERT_EQ(value.traits.value, 1);
    ASSERT_EQ(true, fs::exists(value.fsPath));

    // Test file handle 1, the file size is 16 bytes
    auto value1 = tableObj.at(1);
    ASSERT_EQ(value1.handle, 1);
    ASSERT_EQ(strcmp(value1.fsPath.c_str(), cksumFile.c_str()), 0);
    ASSERT_EQ(static_cast<uint32_t>(fs::file_size(value1.fsPath)), 16);
    ASSERT_EQ(value1.traits.value, 4);
    ASSERT_EQ(true, fs::exists(value1.fsPath));

    // Test invalid file handle
    ASSERT_THROW(tableObj.at(2), std::out_of_range);
}

TEST_F(TestFileTable, ValidateFileTable)
{
    FileTable tableObj(fileTableConfig.c_str());

    // Validate file attribute table
    auto table = tableObj();
    ASSERT_EQ(true,
              std::equal(attrTable.begin(), attrTable.end(), table.begin()));
}

TEST_F(TestFileTable, GetFileTableCommand)
{
    // Initialise the file table with a valid handle of 0 & 1
    auto& table = buildFileTable(fileTableConfig.c_str());

    uint32_t transferHandle = 0;
    uint8_t opFlag = 0;
    uint8_t type = PLDM_FILE_ATTRIBUTE_TABLE;
    uint32_t nextTransferHandle = 0;
    uint8_t transferFlag = PLDM_START_AND_END;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_GET_FILE_TABLE_REQ_BYTES>
        requestMsg{};
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_get_file_table_req*>(
        requestMsg.data() + sizeof(pldm_msg_hdr));
    request->transfer_handle = transferHandle;
    request->operation_flag = opFlag;
    request->table_type = type;

    auto response = getFileTable(requestMsgPtr, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_SUCCESS);
    size_t offsetSize = sizeof(responsePtr->payload[0]);
    ASSERT_EQ(0, memcmp(responsePtr->payload + offsetSize, &nextTransferHandle,
                        sizeof(nextTransferHandle)));
    offsetSize += sizeof(nextTransferHandle);
    ASSERT_EQ(0, memcmp(responsePtr->payload + offsetSize, &transferFlag,
                        sizeof(transferFlag)));
    offsetSize += sizeof(transferFlag);
    ASSERT_EQ(0, memcmp(responsePtr->payload + offsetSize, attrTable.data(),
                        attrTable.size()));
    table.clear();
}

TEST_F(TestFileTable, GetFileTableCommandReqLengthMismatch)
{
    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_GET_FILE_TABLE_REQ_BYTES>
        requestMsg{};
    auto request = reinterpret_cast<pldm_msg*>(requestMsg.data());

    // Pass invalid command payload length
    auto response = getFileTable(request, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR_INVALID_LENGTH);
}

TEST_F(TestFileTable, GetFileTableCommandOEMAttrTable)
{
    uint32_t transferHandle = 0;
    uint8_t opFlag = 0;
    uint8_t type = PLDM_OEM_FILE_ATTRIBUTE_TABLE;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_GET_FILE_TABLE_REQ_BYTES>
        requestMsg{};
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_get_file_table_req*>(
        requestMsg.data() + sizeof(pldm_msg_hdr));
    request->transfer_handle = transferHandle;
    request->operation_flag = opFlag;
    request->table_type = type;

    auto response = getFileTable(requestMsgPtr, requestPayloadLength);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_FILE_TABLE_TYPE);
}

TEST_F(TestFileTable, ReadFileBadPath)
{
    uint32_t fileHandle = 1;
    uint32_t offset = 0;
    uint32_t length = 0x4;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_READ_FILE_REQ_BYTES>
        requestMsg{};
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto payload_length = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_read_file_req*>(requestMsg.data() +
                                                         sizeof(pldm_msg_hdr));

    request->file_handle = fileHandle;
    request->offset = offset;
    request->length = length;

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());

    // Invalid payload length
    auto response = readFile(requestMsgPtr, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR_INVALID_LENGTH);

    // Data out of range. File size is 1024, offset = 1024 is invalid.
    request->offset = 1024;

    response = readFile(requestMsgPtr, payload_length);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_DATA_OUT_OF_RANGE);

    // Invalid file handle
    request->file_handle = 2;

    response = readFile(requestMsgPtr, payload_length);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_FILE_HANDLE);

    table.clear();
}

TEST_F(TestFileTable, ReadFileGoodPath)
{
    uint32_t fileHandle = 0;
    uint32_t offset = 0;
    uint32_t length = 0x4;

    std::array<uint8_t, sizeof(pldm_msg_hdr) + PLDM_READ_FILE_REQ_BYTES>
        requestMsg{};
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto payload_length = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_read_file_req*>(requestMsg.data() +
                                                         sizeof(pldm_msg_hdr));

    request->file_handle = fileHandle;
    request->offset = offset;
    request->length = length;

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());
    FileEntry value{};
    value = table.at(fileHandle);

    std::ifstream stream(value.fsPath, std::ios::in | std::ios::binary);
    stream.seekg(offset);
    std::vector<char> buffer(length);
    stream.read(buffer.data(), length);

    auto responseMsg = readFile(requestMsgPtr, payload_length);
    auto response = reinterpret_cast<pldm_read_file_resp*>(
        responseMsg.data() + sizeof(pldm_msg_hdr));
    ASSERT_EQ(response->completion_code, PLDM_SUCCESS);
    ASSERT_EQ(response->length, length);
    ASSERT_EQ(0, memcmp(response->file_data, buffer.data(), length));

    // Test condition offset + length > fileSize;
    size_t fileSize = 1024;
    request->offset = 1023;
    request->length = 10;

    stream.seekg(request->offset);
    buffer.resize(fileSize - request->offset);
    stream.read(buffer.data(), (fileSize - request->offset));

    responseMsg = readFile(requestMsgPtr, payload_length);
    response = reinterpret_cast<pldm_read_file_resp*>(responseMsg.data() +
                                                      sizeof(pldm_msg_hdr));
    ASSERT_EQ(response->completion_code, PLDM_SUCCESS);
    ASSERT_EQ(response->length, (fileSize - request->offset));
    ASSERT_EQ(0, memcmp(response->file_data, buffer.data(),
                        (fileSize - request->offset)));

    table.clear();
}

TEST_F(TestFileTable, WriteFileBadPath)
{
    uint32_t fileHandle = 0;
    uint32_t offset = 0;
    uint32_t length = 0x10;

    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_WRITE_FILE_REQ_BYTES + length);
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto payload_length = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_write_file_req*>(requestMsg.data() +
                                                          sizeof(pldm_msg_hdr));

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());

    request->file_handle = fileHandle;
    request->offset = offset;
    request->length = length;

    // Invalid payload length
    auto response = writeFile(requestMsgPtr, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_ERROR_INVALID_LENGTH);

    // Data out of range. File size is 1024, offset = 1024 is invalid.
    request->offset = 1024;

    response = writeFile(requestMsgPtr, payload_length);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_DATA_OUT_OF_RANGE);

    // Invalid file handle
    request->file_handle = 2;

    response = writeFile(requestMsgPtr, payload_length);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());
    ASSERT_EQ(responsePtr->payload[0], PLDM_INVALID_FILE_HANDLE);

    table.clear();
}

TEST_F(TestFileTable, WriteFileGoodPath)
{
    uint32_t fileHandle = 1;
    uint32_t offset = 0;
    std::array<uint8_t, 4> fileData = {0x41, 0x42, 0x43, 0x44};
    uint32_t length = fileData.size();

    std::vector<uint8_t> requestMsg(sizeof(pldm_msg_hdr) +
                                    PLDM_WRITE_FILE_REQ_BYTES + length);
    auto requestMsgPtr = reinterpret_cast<pldm_msg*>(requestMsg.data());
    auto payload_length = requestMsg.size() - sizeof(pldm_msg_hdr);
    auto request = reinterpret_cast<pldm_write_file_req*>(requestMsg.data() +
                                                          sizeof(pldm_msg_hdr));

    using namespace pldm::filetable;
    // Initialise the file table with 2 valid file handles 0 & 1.
    auto& table = buildFileTable(fileTableConfig.c_str());
    FileEntry value{};
    value = table.at(fileHandle);

    request->file_handle = fileHandle;
    request->offset = offset;
    request->length = length;
    memcpy(request->file_data, fileData.data(), fileData.size());

    auto responseMsg = writeFile(requestMsgPtr, payload_length);
    auto response = reinterpret_cast<pldm_read_file_resp*>(
        responseMsg.data() + sizeof(pldm_msg_hdr));

    std::ifstream stream(value.fsPath, std::ios::in | std::ios::binary);
    stream.seekg(offset);
    std::vector<char> buffer(length);
    stream.read(buffer.data(), length);

    ASSERT_EQ(response->completion_code, PLDM_SUCCESS);
    ASSERT_EQ(response->length, length);
    ASSERT_EQ(0, memcmp(fileData.data(), buffer.data(), length));

    table.clear();
}

TEST_F(TestFileTable, testAllreadFileByTypeHandler)
{
    using namespace pldm::responder::oem_file_type;
    const auto hdr_size = sizeof(pldm_msg_hdr);
    std::array<uint8_t, hdr_size + PLDM_RW_FILE_TYPE_REQ_BYTES> requestMsg{};
    auto payload_length = requestMsg.size() - hdr_size;
    auto req = reinterpret_cast<pldm_msg*>(requestMsg.data());
    struct pldm_read_write_file_type_req* request =
        reinterpret_cast<struct pldm_read_write_file_type_req*>(req->payload);
    request->file_type = PLDM_FILE_LID;
    request->file_handle = 0;
    request->offset = 0;
    request->length = 13;

    auto response = readFileByTypeHandler(req, 0, readFilePath.c_str());
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    struct pldm_read_write_file_type_resp* resp =
        reinterpret_cast<struct pldm_read_write_file_type_resp*>(
            responsePtr->payload);
    ASSERT_EQ(PLDM_ERROR_INVALID_LENGTH, resp->completion_code);

    request->offset = 1000;
    response = readFileByTypeHandler(req, payload_length, readFilePath.c_str());
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    resp = reinterpret_cast<struct pldm_read_write_file_type_resp*>(
        responsePtr->payload);
    ASSERT_EQ(PLDM_INVALID_FILE_HANDLE, resp->completion_code);

    request->file_handle = 0x80a02023;
    response = readFileByTypeHandler(req, payload_length, readFilePath.c_str());
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    resp = reinterpret_cast<struct pldm_read_write_file_type_resp*>(
        responsePtr->payload);
    ASSERT_EQ(PLDM_DATA_OUT_OF_RANGE, resp->completion_code);

    request->offset = 26;

    response = readFileByTypeHandler(req, payload_length, readFilePath.c_str());
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    resp = reinterpret_cast<struct pldm_read_write_file_type_resp*>(
        responsePtr->payload);
    ASSERT_EQ(PLDM_SUCCESS, resp->completion_code);
}

TEST(writeFileByTypeFromMemory, testBadPath)
{
    using namespace pldm::responder::oem_file_type;
    const auto hdr_size = sizeof(pldm_msg_hdr);
    std::array<uint8_t, hdr_size + PLDM_RW_FILE_TYPE_MEM_REQ_BYTES>
        requestMsg{};
    auto req = reinterpret_cast<pldm_msg*>(requestMsg.data());
    size_t requestPayloadLength = requestMsg.size() - hdr_size;
    struct pldm_read_write_file_type_memory_req* request =
        reinterpret_cast<struct pldm_read_write_file_type_memory_req*>(
            req->payload);
    request->file_type = PLDM_FILE_ERROR_LOG;
    request->file_handle = 0xFFFFFFFF;
    request->offset = 0;
    request->length = 17;
    request->address = 0;

    auto response = writeFileByTypeFromMemory(req, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    struct pldm_read_write_file_type_memory_resp* resp =
        reinterpret_cast<struct pldm_read_write_file_type_memory_resp*>(
            responsePtr->payload);
    ASSERT_EQ(PLDM_ERROR_INVALID_LENGTH, resp->completion_code);

    response = writeFileByTypeFromMemory(req, requestPayloadLength);
    responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    resp = reinterpret_cast<struct pldm_read_write_file_type_memory_resp*>(
        responsePtr->payload);
    ASSERT_EQ(PLDM_INVALID_WRITE_LENGTH, resp->completion_code);
}

TEST(readFileByTypeIntoMemory, testBadPath)
{
    using namespace pldm::responder::oem_file_type;
    const auto hdr_size = sizeof(pldm_msg_hdr);
    std::array<uint8_t, hdr_size + PLDM_RW_FILE_TYPE_MEM_REQ_BYTES>
        requestMsg{};
    auto req = reinterpret_cast<pldm_msg*>(requestMsg.data());
    struct pldm_read_write_file_type_memory_req* request =
        reinterpret_cast<struct pldm_read_write_file_type_memory_req*>(
            req->payload);
    request->file_type = PLDM_FILE_LID;
    request->file_handle = 0;
    request->offset = 0;
    request->length = 17;
    request->address = 0;

    auto response = readFileByTypeIntoMemory(req, 0);
    auto responsePtr = reinterpret_cast<pldm_msg*>(response.data());

    struct pldm_read_write_file_type_memory_resp* resp =
        reinterpret_cast<struct pldm_read_write_file_type_memory_resp*>(
            responsePtr->payload);
    ASSERT_EQ(PLDM_ERROR_INVALID_LENGTH, resp->completion_code);
}
