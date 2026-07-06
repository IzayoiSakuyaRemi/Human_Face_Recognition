/* Stub: no assets on S3 */
#pragma once
class Assets { public: static Assets& GetInstance() { static Assets a; return a; } bool partition_valid() const { return false; } };
