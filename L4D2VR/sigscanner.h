#pragma once
#include <Windows.h>
#include <psapi.h>
#include <vector>
#include <sstream>


class SigScanner
{
public:
	// Returns 0 if current offset matches, -1 if no matches found.
	// A value > 0 is the new offset.
	static int VerifyOffset(std::string hookName, std::string moduleName, int currentOffset, std::string signature, int sigOffset = 0)
	{
		HMODULE hModule = GetModuleHandle(moduleName.c_str());
		MODULEINFO moduleInfo;
		GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));

		uint8_t *bytes = (uint8_t *)moduleInfo.lpBaseOfDll;

		std::vector<int> pattern;

		std::stringstream ss(signature);
		std::string sigByte;
		while (ss >> sigByte)
		{
			if (sigByte == "?" || sigByte == "??")
				pattern.push_back(-1);
			else
				pattern.push_back(strtoul(sigByte.c_str(), NULL, 16));
		}

		int patternLen = pattern.size();

		// Check if current offset is good
		bool offsetMatchesSig = true;
		for (int i = 0; i < patternLen; ++i)
		{
			if ( (bytes[currentOffset - sigOffset + i] != pattern[i]) && (pattern[i] != -1) )
			{
				offsetMatchesSig = false;
				break;
			}
		}

		if (offsetMatchesSig)
			return 0;

		// Scan the dll for new offset
		for (int i = 0; i < moduleInfo.SizeOfImage; ++i)
		{
			bool found = true;
			for (int j = 0; j < patternLen; ++j)
			{
				if ((bytes[i + j] != pattern[j]) && (pattern[j] != -1))
				{
					found = false;
					break;
				}
			}
			if (found)
			{
				return i + sigOffset;
			}
		}

		uint8_t* testAddr = bytes + (currentOffset - sigOffset);
		DumpBytes(hookName, testAddr, patternLen, signature);
		return -1;
	}

	static void DumpBytes(std::string hookName, uint8_t* addr, int length, const std::string& signature)
	{
		std::vector<int> sig;
		std::istringstream ss(signature);
		std::string token;

		while (ss >> token)
		{
			if (token == "?" || token == "??")
				sig.push_back(-1);
			else
				sig.push_back(std::stoi(token, nullptr, 16));
		}

		int maxLen = std::min(length, (int)sig.size());

		std::ostringstream msg;

		msg << hookName << ": Signature mismatch\n";
		msg << "expected: ";

		for (int i = 0; i < maxLen; i++)
		{
			if (sig[i] == -1)
				msg << "?? ";
			else
				msg << std::hex << std::setw(2) << std::setfill('0')
				<< sig[i] << " ";
		}

		msg << "\nactual:   ";
		msg << std::dec;

		for (int i = 0; i < maxLen; i++)
		{
			uint8_t actual = addr[i];
			int expected = sig[i];

			bool match = (expected == -1) || (actual == (uint8_t)expected);

			if (!match)
				msg << "[";

			msg << std::hex << std::setw(2) << std::setfill('0') << (int)actual;

			if (!match)
				msg << "]";

			msg << " ";
		}

		msg << std::dec;
		g_Game->errorMsg(msg.str().c_str());
	}
};