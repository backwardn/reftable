// Copyright 2019 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef CONSTANTS_H
#define CONSTANTS_H

#define HASH_SIZE 20
#define VERSION 1
#define HEADER_SIZE 24
#define FOOTER_SIZE 68

#define BLOCK_TYPE_LOG 'g'
#define BLOCK_TYPE_INDEX 'i'
#define BLOCK_TYPE_REF 'r'
#define BLOCK_TYPE_OBJ 'o'
#define BLOCK_TYPE_ANY 0

#define MAX_RESTARTS ((1 << 16) - 1)

#endif
