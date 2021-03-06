/*
 * Copyright (C) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "test.h"
#include "gtest/gtest.h"
#include "runtime/helpers/file_io.h"
#include "runtime/utilities/debug_file_reader.h"

#include <memory>
#include <string>

using namespace OCLRT;
using namespace std;

class TestSettingsFileReader : public SettingsFileReader {
  public:
    TestSettingsFileReader(const char *filePath = nullptr) : SettingsFileReader(filePath) {
    }

    ~TestSettingsFileReader() override {
    }

    size_t getValueSettingsCount() {
        return settingValueMap.size();
    }

    size_t getStringSettingsCount() {
        return settingStringMap.size();
    }

  public:
    static const char *testPath;
    static const char *stringTestPath;
};

const char *TestSettingsFileReader::testPath = "./test_files/igdrcl.config";
const char *TestSettingsFileReader::stringTestPath = "./test_files/igdrcl_string.config";

TEST(SettingsFileReader, CreateFileReaderWithoutFile) {
    bool settingsFileExists = fileExists(SettingsReader::settingsFileName);

    // if settings file exists, remove it
    if (settingsFileExists) {
        remove(SettingsReader::settingsFileName);
    }

    // Use current location for file read
    std::unique_ptr<TestSettingsFileReader> reader = unique_ptr<TestSettingsFileReader>(new TestSettingsFileReader());
    ASSERT_NE(nullptr, reader);

    EXPECT_EQ(0u, reader->getValueSettingsCount());
    EXPECT_EQ(0u, reader->getStringSettingsCount());
}

TEST(SettingsFileReader, GetStringSettingFromFile) {
    // Use test settings file
    std::unique_ptr<TestSettingsFileReader> reader = unique_ptr<TestSettingsFileReader>(new TestSettingsFileReader(TestSettingsFileReader::stringTestPath));
    ASSERT_NE(nullptr, reader);

    string retValue;
    // StringTestKey is defined in file: unit_tests\helpers\TestDebugVariables.inl
    string returnedStringValue = reader->getSetting("StringTestKey", retValue);

    // "Test Value" is a value that should be read from file defined in stringTestPath member
    EXPECT_STREQ(returnedStringValue.c_str(), "TestValue");

#define DECLARE_DEBUG_VARIABLE(dataType, variableName, defaultValue, description) \
    {                                                                             \
        dataType defaultData = defaultValue;                                      \
        dataType tempData = reader->getSetting(#variableName, defaultData);       \
        if (tempData == defaultData) {                                            \
            EXPECT_TRUE(true);                                                    \
        }                                                                         \
    }
#include "unit_tests/helpers/TestDebugVariables.inl"
#undef DECLARE_DEBUG_VARIABLE
}

TEST(SettingsFileReader, givenDebugFileSettingInWhichStringIsFollowedByIntegerWhenItIsParsedThenProperValuesAreObtained) {
    std::unique_ptr<TestSettingsFileReader> reader(new TestSettingsFileReader(TestSettingsFileReader::stringTestPath));
    ASSERT_NE(nullptr, reader.get());

    int32_t retValue = 0;
    int32_t returnedIntValue = reader->getSetting("IntTestKey", retValue);

    EXPECT_EQ(1, returnedIntValue);

    string retValueString;
    string returnedStringValue = reader->getSetting("StringTestKey", retValueString);

    EXPECT_STREQ(returnedStringValue.c_str(), "TestValue");
}

TEST(SettingsFileReader, GetSettingWhenNotInFile) {

    // Use test settings file
    std::unique_ptr<TestSettingsFileReader> reader = unique_ptr<TestSettingsFileReader>(new TestSettingsFileReader(TestSettingsFileReader::testPath));
    ASSERT_NE(nullptr, reader);

    bool defaultBoolValue = false;
    bool returnedBoolValue = reader->getSetting("BoolSettingNotExistingInFile", defaultBoolValue);

    EXPECT_EQ(defaultBoolValue, returnedBoolValue);

    int32_t defaultIntValue = 123;
    int32_t returnedIntValue = reader->getSetting("IntSettingNotExistingInFile", defaultIntValue);

    EXPECT_EQ(defaultIntValue, returnedIntValue);

    string defaultStringValue = "ABCD";
    string returnedStringValue = reader->getSetting("StringSettingNotExistingInFile", defaultStringValue);

    EXPECT_EQ(defaultStringValue, returnedStringValue);
}
