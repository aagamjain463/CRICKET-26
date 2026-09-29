// Runs the engine-only auction tests headless (see shim/CoreMinimal.h). Usage: run.sh [-v] [TestNameFilter]
#include "Misc/AutomationTest.h"
#include <chrono>

int main(int Argc, char** Argv)
{
	std::string Filter;
	for (int I = 1; I < Argc; ++I)
	{
		if (std::string(Argv[I]) == "-v") FAutomationTestBase::bVerbose = true;
		else Filter = Argv[I];
	}
	int Failed = 0, Ran = 0;
	for (FAutomationTestBase* T : FAutomationTestBase::Registry())
	{
		if (!Filter.empty() && std::string(T->Name).find(Filter) == std::string::npos) continue;
		const auto Start = std::chrono::steady_clock::now();
		std::cout << T->Name << "\n";
		const bool bOk = T->RunTest(FString()) && T->Errors == 0;
		const double Ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
		std::cout << "  " << (bOk ? "Success" : "FAILED") << "  (" << int(Ms) << " ms)\n";
		Failed += !bOk;
		++Ran;
	}
	std::cout << Ran - Failed << "/" << Ran << " passed\n";
	return Failed ? 1 : 0;
}
