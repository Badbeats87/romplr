#include "kernel.h"

int main (void)
{
	CKernel Kernel;

	if (!Kernel.Initialize ())
	{
		return ShutdownHalt;
	}

	return Kernel.Run ();
}
