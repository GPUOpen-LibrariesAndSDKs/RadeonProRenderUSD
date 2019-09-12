#ifndef BOOST_INCLUDE_PATH_H
#define BOOST_INCLUDE_PATH_H

#define __STRINGIZE_PATHX(x) #x
#define __STRINGIZE_PATH(x) __STRINGIZE_PATHX(x)

#ifdef BUILD_AS_HOUDINI_PLUGIN
# define BOOST_NS hboost
# define BOOST_INCLUDE_PATH(suffix) __STRINGIZE_PATH(hboost/suffix)
#else
# define BOOST_NS boost
# define BOOST_INCLUDE_PATH(suffix) __STRINGIZE_PATH(boost/suffix)
#endif

#endif // BOOST_INCLUDE_PATH_H
