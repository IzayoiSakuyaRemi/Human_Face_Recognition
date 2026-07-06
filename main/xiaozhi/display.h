/* Stub: no display on S3 */
#pragma once
class Display { public: virtual ~Display() = default; };
class NoDisplay : public Display {};
