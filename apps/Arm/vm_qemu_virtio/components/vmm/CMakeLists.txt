#
# Copyright 2022, Technology Innovation Institute
#
# SPDX-License-Identifier: BSD-2-Clause
#

cmake_minimum_required(VERSION 3.8.2)

project(vmm C)

file(GLOB sources src/*.c)

DeclareCAmkESComponent(vmm
    SOURCES ${sources}
    INCLUDES include)
