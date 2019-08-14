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

#if defined(ARCH_OS_WINDOWS)
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif

# ifdef BUILD_AS_HOUDINI_PLUGIN
#  include <hboost/preprocessor/variadic/size.hpp>
#  include <hboost/vmd/is_empty.hpp>
#  include <hboost/vmd/is_tuple.hpp>
# else
#  include <boost/preprocessor/variadic/size.hpp>
#  include <boost/vmd/is_empty.hpp>
#  include <boost/vmd/is_tuple.hpp>
# endif
#endif

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

#ifdef BUILD_AS_HOUDINI_PLUGIN
# include <hboost/any.hpp>
# include <hboost/call_traits.hpp>
# include <hboost/function.hpp>
# include <hboost/functional/hash_fwd.hpp>
# include <hboost/intrusive_ptr.hpp>
# include <hboost/mpl/empty.hpp>
# include <hboost/mpl/front.hpp>
# include <hboost/mpl/if.hpp>
# include <hboost/mpl/pop_front.hpp>
# include <hboost/mpl/remove.hpp>
# include <hboost/mpl/vector.hpp>
# include <hboost/noncopyable.hpp>
# include <hboost/operators.hpp>
# include <hboost/optional.hpp>
# include <hboost/preprocessor/arithmetic/add.hpp>
# include <hboost/preprocessor/arithmetic/inc.hpp>
# include <hboost/preprocessor/arithmetic/sub.hpp>
# include <hboost/preprocessor/cat.hpp>
# include <hboost/preprocessor/comparison/equal.hpp>
# include <hboost/preprocessor/control/expr_iif.hpp>
# include <hboost/preprocessor/control/iif.hpp>
# include <hboost/preprocessor/facilities/expand.hpp>
# include <hboost/preprocessor/logical/and.hpp>
# include <hboost/preprocessor/logical/not.hpp>
# include <hboost/preprocessor/punctuation/comma.hpp>
# include <hboost/preprocessor/punctuation/comma_if.hpp>
# include <hboost/preprocessor/punctuation/paren.hpp>
# include <hboost/preprocessor/repetition/repeat.hpp>
# include <hboost/preprocessor/seq/filter.hpp>
# include <hboost/preprocessor/seq/for_each.hpp>
# include <hboost/preprocessor/seq/for_each_i.hpp>
# include <hboost/preprocessor/seq/push_back.hpp>
# include <hboost/preprocessor/seq/size.hpp>
# include <hboost/preprocessor/stringize.hpp>
# include <hboost/preprocessor/tuple/eat.hpp>
# include <hboost/preprocessor/tuple/elem.hpp>
# include <hboost/preprocessor/tuple/to_list.hpp>
# include <hboost/preprocessor/tuple/to_seq.hpp>
# include <hboost/scoped_ptr.hpp>
# include <hboost/shared_ptr.hpp>
# include <hboost/type_traits/is_base_of.hpp>
# include <hboost/type_traits/is_const.hpp>
# include <hboost/type_traits/is_convertible.hpp>
# include <hboost/type_traits/is_enum.hpp>
# include <hboost/type_traits/is_same.hpp>
# include <hboost/unordered_map.hpp>
# include <hboost/utility/enable_if.hpp>
# include <hboost/weak_ptr.hpp>
#else
# include <boost/any.hpp>
# include <boost/call_traits.hpp>
# include <boost/function.hpp>
# include <boost/functional/hash_fwd.hpp>
# include <boost/intrusive_ptr.hpp>
# include <boost/mpl/empty.hpp>
# include <boost/mpl/front.hpp>
# include <boost/mpl/if.hpp>
# include <boost/mpl/pop_front.hpp>
# include <boost/mpl/remove.hpp>
# include <boost/mpl/vector.hpp>
# include <boost/noncopyable.hpp>
# include <boost/operators.hpp>
# include <boost/optional.hpp>
# include <boost/preprocessor/arithmetic/add.hpp>
# include <boost/preprocessor/arithmetic/inc.hpp>
# include <boost/preprocessor/arithmetic/sub.hpp>
# include <boost/preprocessor/cat.hpp>
# include <boost/preprocessor/comparison/equal.hpp>
# include <boost/preprocessor/control/expr_iif.hpp>
# include <boost/preprocessor/control/iif.hpp>
# include <boost/preprocessor/facilities/expand.hpp>
# include <boost/preprocessor/logical/and.hpp>
# include <boost/preprocessor/logical/not.hpp>
# include <boost/preprocessor/punctuation/comma.hpp>
# include <boost/preprocessor/punctuation/comma_if.hpp>
# include <boost/preprocessor/punctuation/paren.hpp>
# include <boost/preprocessor/repetition/repeat.hpp>
# include <boost/preprocessor/seq/filter.hpp>
# include <boost/preprocessor/seq/for_each.hpp>
# include <boost/preprocessor/seq/for_each_i.hpp>
# include <boost/preprocessor/seq/push_back.hpp>
# include <boost/preprocessor/seq/size.hpp>
# include <boost/preprocessor/stringize.hpp>
# include <boost/preprocessor/tuple/eat.hpp>
# include <boost/preprocessor/tuple/elem.hpp>
# include <boost/preprocessor/tuple/to_list.hpp>
# include <boost/preprocessor/tuple/to_seq.hpp>
# include <boost/scoped_ptr.hpp>
# include <boost/shared_ptr.hpp>
# include <boost/type_traits/is_base_of.hpp>
# include <boost/type_traits/is_const.hpp>
# include <boost/type_traits/is_convertible.hpp>
# include <boost/type_traits/is_enum.hpp>
# include <boost/type_traits/is_same.hpp>
# include <boost/unordered_map.hpp>
# include <boost/utility/enable_if.hpp>
# include <boost/weak_ptr.hpp>
#endif
