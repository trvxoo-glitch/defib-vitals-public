#pragma once
// Small SHA-256 (public-domain style implementation) for file hashes in the plugin inventory.
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace Vitals
{
	class Sha256
	{
	public:
		Sha256() { Reset(); }

		void Update(const std::uint8_t* a_data, std::size_t a_len)
		{
			_total += a_len;
			while (a_len) {
				const auto n = std::min<std::size_t>(a_len, 64 - _fill);
				std::memcpy(_buf + _fill, a_data, n);
				_fill += n;
				a_data += n;
				a_len -= n;
				if (_fill == 64) {
					Block(_buf);
					_fill = 0;
				}
			}
		}

		std::string Final()
		{
			const std::uint64_t bits = _total * 8;
			std::uint8_t        pad = 0x80;
			Update(&pad, 1);
			pad = 0;
			while (_fill != 56)
				Update(&pad, 1);
			std::uint8_t len[8];
			for (int i = 0; i < 8; ++i)
				len[i] = static_cast<std::uint8_t>(bits >> (56 - 8 * i));
			Update(len, 8);
			static constexpr char hex[] = "0123456789abcdef";
			std::string out;
			for (const auto h : _h)
				for (int i = 28; i >= 0; i -= 4)
					out += hex[(h >> i) & 0xF];
			Reset();
			return out;
		}

	private:
		void Reset()
		{
			_h = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
			_fill = 0;
			_total = 0;
		}

		static std::uint32_t Rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

		void Block(const std::uint8_t* p)
		{
			static constexpr std::uint32_t k[64] = {
				0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
				0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
				0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
				0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
				0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
				0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
				0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
				0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
			};
			std::uint32_t w[64];
			for (int i = 0; i < 16; ++i)
				w[i] = (std::uint32_t(p[i * 4]) << 24) | (std::uint32_t(p[i * 4 + 1]) << 16) | (std::uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
			for (int i = 16; i < 64; ++i) {
				const auto s0 = Rotr(w[i - 15], 7) ^ Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const auto s1 = Rotr(w[i - 2], 17) ^ Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			auto [a, b, c, d, e, f, g, h] = _h;
			for (int i = 0; i < 64; ++i) {
				const auto t1 = h + (Rotr(e, 6) ^ Rotr(e, 11) ^ Rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				const auto t2 = (Rotr(a, 2) ^ Rotr(a, 13) ^ Rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			_h[0] += a; _h[1] += b; _h[2] += c; _h[3] += d; _h[4] += e; _h[5] += f; _h[6] += g; _h[7] += h;
		}

		std::array<std::uint32_t, 8> _h{};
		std::uint8_t                 _buf[64]{};
		std::size_t                  _fill{ 0 };
		std::uint64_t                _total{ 0 };
	};
}
