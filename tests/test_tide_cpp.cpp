#include <gtest/gtest.h>

#include "tide/tide.hpp"

TEST(TideTest, TestBMIPointerAllocation) {
    cece::io::Tide tide;
    // Attempting to call without config should fail or throw
    EXPECT_THROW(tide.Initialize("non_existent_file.yaml"), std::runtime_error);
}
