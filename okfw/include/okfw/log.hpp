// okfw — levelled logging to stderr. Stdout is reserved for the JSON result.
#pragma once

#include <string>

namespace vyzorix::okfw {

enum class Level { Debug, Info, Error };

// set_level gates output; the default is Info. Debug is for the -v flag.
void set_level(Level level);

// log writes one line to stderr at the given level.
void log(Level level, const std::string& message);

inline void info(const std::string& m) { log(Level::Info, m); }

// log_error, not error: okfw::error(message) is the Error factory in result.hpp.
inline void log_error(const std::string& m) { log(Level::Error, m); }

// redact scrubs anything that looks like a credential or a signed URL before it
// reaches stderr (contract R19): an Authorization header value, a bearer/basic
// token, a common token prefix, a `?token=`/`?sig=`/`?X-Amz-Signature=` query
// parameter, and a `user:pass@host` userinfo segment. It is applied to every
// log line, so a caller cannot forget it.
[[nodiscard]] std::string redact(const std::string& text);

}  // namespace vyzorix::okfw
