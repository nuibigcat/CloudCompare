// Example of a GL filter for the GL filter plugin

#include "Bilateral.h"

#include "ccBilateralFilter.h"

#include <QInputDialog>
#include <QtMath>

namespace Example
{
	ccGlFilter* getBilateral()
	{
		bool   ok    = false;
		double sigma = QInputDialog::getDouble(nullptr,
		                                       "Bilateral filter",
		                                       "Sigma (pixel)",
		                                       1.0,
		                                       0.1,
		                                       8.0,
		                                       1,
		                                       &ok);

		if (!ok || sigma < 0)
		{
			return nullptr;
		}

		unsigned int halfFilterSize = static_cast<unsigned int>(qCeil(2.5 * sigma));

		ccBilateralFilter* filter = new ccBilateralFilter;

		filter->setParams(halfFilterSize, static_cast<float>(sigma), 0.0f);

		return filter;
	}
} // namespace Example
