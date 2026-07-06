/* Stub: no LED on S3 */
#pragma once
class Led { public: virtual ~Led() = default; virtual void OnStateChanged() = 0; };
class NoLed : public Led { public: void OnStateChanged() override {} };
