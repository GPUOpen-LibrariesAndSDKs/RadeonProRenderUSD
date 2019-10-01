//
// Copyright 2017 Pixar
//
// Licensed under the Apache License, Version 2.0 (the "Apache License")
// with the following modification; you may not use this file except in
// compliance with the Apache License and the following modification to it:
// Section 6. Trademarks. is deleted and replaced with:
//
// 6. Trademarks. This License does not grant permission to use the trade
//    names, trademarks, service marks, or product names of the Licensor
//    and its affiliates, except as required to comply with Section 4(c) of
//    the License and to reproduce the content of the NOTICE file.
//
// You may obtain a copy of the Apache License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the Apache License with the above modification is
// distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied. See the Apache License for the specific
// language governing permissions and limitations under the Apache License.
//
// WARNING: THIS FILE IS GENERATED.  DO NOT EDIT.
//

#define TF_MAX_ARITY 7
#include "pxr/pxr.h"
#include "pxr/base/arch/defines.h"
#include "boostIncludePath.h"

#if defined(ARCH_OS_WINDOWS)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif

# include BOOST_INCLUDE_PATH(preprocessor/variadic/size.hpp)
# include BOOST_INCLUDE_PATH(vmd/is_empty.hpp)
# include BOOST_INCLUDE_PATH(vmd/is_tuple.hpp)

#endif // defined(ARCH_OS_WINDOWS)

#include BOOST_INCLUDE_PATH(any.hpp)
#include BOOST_INCLUDE_PATH(call_traits.hpp)
#include BOOST_INCLUDE_PATH(function.hpp)
#include BOOST_INCLUDE_PATH(functional/hash_fwd.hpp)
#include BOOST_INCLUDE_PATH(intrusive_ptr.hpp)
#include BOOST_INCLUDE_PATH(mpl/empty.hpp)
#include BOOST_INCLUDE_PATH(mpl/front.hpp)
#include BOOST_INCLUDE_PATH(mpl/if.hpp)
#include BOOST_INCLUDE_PATH(mpl/pop_front.hpp)
#include BOOST_INCLUDE_PATH(mpl/remove.hpp)
#include BOOST_INCLUDE_PATH(mpl/vector.hpp)
#include BOOST_INCLUDE_PATH(noncopyable.hpp)
#include BOOST_INCLUDE_PATH(operators.hpp)
#include BOOST_INCLUDE_PATH(optional.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/arithmetic/add.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/arithmetic/inc.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/arithmetic/sub.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/cat.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/comparison/equal.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/control/expr_iif.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/control/iif.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/facilities/expand.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/logical/and.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/logical/not.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/punctuation/comma.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/punctuation/comma_if.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/punctuation/paren.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/repetition/repeat.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/seq/filter.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/seq/for_each.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/seq/for_each_i.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/seq/push_back.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/seq/size.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/stringize.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/tuple/eat.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/tuple/elem.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/tuple/to_list.hpp)
#include BOOST_INCLUDE_PATH(preprocessor/tuple/to_seq.hpp)
#include BOOST_INCLUDE_PATH(scoped_ptr.hpp)
#include BOOST_INCLUDE_PATH(shared_ptr.hpp)
#include BOOST_INCLUDE_PATH(type_traits/is_base_of.hpp)
#include BOOST_INCLUDE_PATH(type_traits/is_const.hpp)
#include BOOST_INCLUDE_PATH(type_traits/is_convertible.hpp)
#include BOOST_INCLUDE_PATH(type_traits/is_enum.hpp)
#include BOOST_INCLUDE_PATH(type_traits/is_same.hpp)
#include BOOST_INCLUDE_PATH(unordered_map.hpp)
#include BOOST_INCLUDE_PATH(utility/enable_if.hpp)
#include BOOST_INCLUDE_PATH(weak_ptr.hpp)

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <inttypes.h>
#include <iosfwd>
#include <iterator>
#include <list>
#include <map>
#include <math.h>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <sys/types.h>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <tbb/atomic.h>
