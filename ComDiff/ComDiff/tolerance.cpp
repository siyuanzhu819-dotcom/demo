#include "tolerance.h"
#include "json.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace
{
	struct RangeValue
	{
		double min;
		double max;
		double value;
	};

	struct SymbolData
	{
		std::string type;
		std::string basis;
		std::string mode;
		bool hasConstantValue = false;
		double constantValue = 0.0;
		std::vector<RangeValue> table;
		std::map<int, std::vector<RangeValue> > grades;
		std::map<int, std::vector<RangeValue> > corrections;
	};

	struct ToleranceData
	{
		std::vector<RangeValue> itRanges;
		std::map<int, std::vector<double> > itValues;
		std::map<std::string, SymbolData> holeSymbols;
		std::map<std::string, SymbolData> shaftSymbols;
	};

	struct FitPart
	{
		std::string zone;
		int grade;
	};

	struct Fit
	{
		FitPart hole;
		FitPart shaft;
	};

	std::string GetSourceDirectory()
	{
		std::string path = __FILE__;
		const size_t slashPos = path.find_last_of("\\/");
		if (slashPos == std::string::npos)
		{
			return ".";
		}

		return path.substr(0, slashPos);
	}

	std::ifstream OpenJsonFile(const std::string& fileName)
	{
		std::ifstream input(fileName.c_str(), std::ios::in | std::ios::binary);
		if (input)
		{
			return input;
		}

		const std::string fallbackPath = GetSourceDirectory() + "\\" + fileName;
		return std::ifstream(fallbackPath.c_str(), std::ios::in | std::ios::binary);
	}

	json LoadJsonFile(const std::string& fileName)
	{
		std::ifstream input = OpenJsonFile(fileName);
		if (!input)
		{
			throw std::runtime_error("Unable to open JSON file: " + fileName);
		}

		json data;
		input >> data;
		return data;
	}

	std::vector<RangeValue> ParseRangeTable(const json& tableJson)
	{
		std::vector<RangeValue> table;

		for (json::const_iterator it = tableJson.begin(); it != tableJson.end(); ++it)
		{
			RangeValue item;
			item.min = (*it).at("min").get<double>();
			item.max = (*it).at("max").get<double>();
			item.value = (*it).at("value").get<double>();
			table.push_back(item);
		}

		return table;
	}

	SymbolData ParseSymbolData(const json& symbolJson)
	{
		SymbolData data;

		if (symbolJson.contains("type"))
		{
			data.type = symbolJson.at("type").get<std::string>();
		}

		if (symbolJson.contains("basis"))
		{
			data.basis = symbolJson.at("basis").get<std::string>();
		}

		if (symbolJson.contains("mode"))
		{
			data.mode = symbolJson.at("mode").get<std::string>();
		}

		if (symbolJson.contains("value"))
		{
			data.hasConstantValue = true;
			data.constantValue = symbolJson.at("value").get<double>();
		}

		if (symbolJson.contains("table"))
		{
			data.table = ParseRangeTable(symbolJson.at("table"));
		}

		if (symbolJson.contains("grades"))
		{
			const json& gradeJson = symbolJson.at("grades");
			for (json::const_iterator it = gradeJson.begin(); it != gradeJson.end(); ++it)
			{
				const int grade = std::stoi(it.key());
				data.grades[grade] = ParseRangeTable(it.value());
			}
		}

		if (symbolJson.contains("corrections"))
		{
			const json& correctionJson = symbolJson.at("corrections");
			for (json::const_iterator it = correctionJson.begin(); it != correctionJson.end(); ++it)
			{
				const int grade = std::stoi(it.key());
				data.corrections[grade] = ParseRangeTable(it.value());
			}
		}

		return data;
	}

	ToleranceData LoadToleranceData()
	{
		ToleranceData data;

		const json itJson = LoadJsonFile("IT.json");
		for (json::const_iterator it = itJson.at("ranges").begin(); it != itJson.at("ranges").end(); ++it)
		{
			RangeValue range;
			range.min = (*it).at("min").get<double>();
			range.max = (*it).at("max").get<double>();
			range.value = 0.0;
			data.itRanges.push_back(range);
		}

		const json& itValuesJson = itJson.at("IT");
		for (json::const_iterator it = itValuesJson.begin(); it != itValuesJson.end(); ++it)
		{
			const int grade = std::stoi(it.key());
			data.itValues[grade] = it.value().get<std::vector<double> >();
		}

		const json holeJson = LoadJsonFile("deviation_hole.json");
		for (json::const_iterator it = holeJson.begin(); it != holeJson.end(); ++it)
		{
			data.holeSymbols[it.key()] = ParseSymbolData(it.value());
		}

		const json shaftJson = LoadJsonFile("deviation_shaft.json");
		const json& shaftSymbolsJson = shaftJson.at("shaft");
		for (json::const_iterator it = shaftSymbolsJson.begin(); it != shaftSymbolsJson.end(); ++it)
		{
			data.shaftSymbols[it.key()] = ParseSymbolData(it.value());
		}

		return data;
	}

	const ToleranceData& GetToleranceData()
	{
		static const ToleranceData data = LoadToleranceData();
		return data;
	}

	size_t FindRangeIndex(double size, const std::vector<RangeValue>& ranges)
	{
		for (size_t i = 0; i < ranges.size(); ++i)
		{
			if (size > ranges[i].min && size <= ranges[i].max)
			{
				return i;
			}
		}

		throw std::runtime_error("Size is out of range");
	}

	double FindRangeValue(double size, const std::vector<RangeValue>& table, const std::string& messagePrefix)
	{
		for (size_t i = 0; i < table.size(); ++i)
		{
			if (size > table[i].min && size <= table[i].max)
			{
				return table[i].value;
			}
		}

		throw std::runtime_error(messagePrefix + " size is out of range");
	}

	bool TryFindRangeValue(double size, const std::vector<RangeValue>& table, double* value)
	{
		for (size_t i = 0; i < table.size(); ++i)
		{
			if (size > table[i].min && size <= table[i].max)
			{
				*value = table[i].value;
				return true;
			}
		}

		return false;
	}

	FitPart ParseFitPart(const std::string& text)
	{
		size_t splitPos = 0;
		while (splitPos < text.size() && std::isalpha(static_cast<unsigned char>(text[splitPos])))
		{
			++splitPos;
		}

		if (splitPos == 0 || splitPos == text.size())
		{
			throw std::runtime_error("Invalid fit token: " + text);
		}

		FitPart part;
		part.zone = text.substr(0, splitPos);
		part.grade = std::stoi(text.substr(splitPos));
		return part;
	}

	Fit ParseFit(const std::string& fitStr)
	{
		const size_t pos = fitStr.find('/');
		if (pos == std::string::npos)
		{
			throw std::runtime_error("Invalid fit string: " + fitStr);
		}

		Fit fit;
		fit.hole = ParseFitPart(fitStr.substr(0, pos));
		fit.shaft = ParseFitPart(fitStr.substr(pos + 1)); 
		return fit;
	}

	double GetIT(double size, int grade)
	{
		const ToleranceData& data = GetToleranceData();
		std::map<int, std::vector<double> >::const_iterator it = data.itValues.find(grade);
		if (it == data.itValues.end())
		{
			throw std::runtime_error("Unsupported IT grade");
		}

		const size_t rangeIndex = FindRangeIndex(size, data.itRanges);
		if (rangeIndex >= it->second.size())
		{
			throw std::runtime_error("IT table data is incomplete");
		}

		return it->second[rangeIndex];
	}

	double GetHoleBaseDeviation(double size, const std::string& zone)
	{
		const ToleranceData& data = GetToleranceData();
		std::map<std::string, SymbolData>::const_iterator it = data.holeSymbols.find(zone);
		if (it == data.holeSymbols.end())
		{
			throw std::runtime_error("Unsupported hole deviation zone: " + zone);
		}

		return FindRangeValue(size, it->second.table, "Hole deviation table");
	}

	bool TryGetHoleGradeDeviation(double size, const std::string& zone, int grade, double* value)
	{
		const ToleranceData& data = GetToleranceData();
		std::map<std::string, SymbolData>::const_iterator it = data.holeSymbols.find(zone);
		if (it == data.holeSymbols.end())
		{
			throw std::runtime_error("Unsupported hole deviation zone: " + zone);
		}

		std::map<int, std::vector<RangeValue> >::const_iterator gradeIt = it->second.grades.find(grade);
		if (gradeIt == it->second.grades.end())
		{
			return false;
		}

		return TryFindRangeValue(size, gradeIt->second, value);
	}

	double GetShaftBaseDeviation(double size, const std::string& zone)
	{
		const ToleranceData& data = GetToleranceData();
		std::map<std::string, SymbolData>::const_iterator it = data.shaftSymbols.find(zone);
		if (it == data.shaftSymbols.end())
		{
			throw std::runtime_error("Unsupported shaft deviation zone: " + zone);
		}

		if (it->second.hasConstantValue)
		{
			return it->second.constantValue;
		}

		if (!it->second.table.empty())
		{
			return FindRangeValue(size, it->second.table, "Shaft deviation table");
		}

		throw std::runtime_error("Incomplete shaft deviation data: " + zone);
	}

	Deviation ComputeHole(double size, const std::string& zone, int grade)
	{
		const double IT = GetIT(size, grade);
		const ToleranceData& data = GetToleranceData();
		std::map<std::string, SymbolData>::const_iterator it = data.holeSymbols.find(zone);
		if (it == data.holeSymbols.end())
		{
			throw std::runtime_error("Unsupported hole deviation zone: " + zone);
		}

		Deviation d;
		if (it->second.mode == "symmetric")
		{
			double directValue = 0.0;
			if (TryGetHoleGradeDeviation(size, zone, grade, &directValue))
			{
				d.upper = directValue;
				d.lower = -directValue;
				return d;
			}

			const double halfTolerance = std::ceil(IT / 2.0);
			d.upper = halfTolerance;
			d.lower = -halfTolerance;
			return d;
		}

		double directValue = 0.0;
		if (TryGetHoleGradeDeviation(size, zone, grade, &directValue))
		{
			if (it->second.basis == "es")
			{
				d.upper = directValue;
				d.lower = directValue - IT;
				return d;
			}

			d.lower = directValue;
			d.upper = directValue + IT;
			return d;
		}

		const double base = GetHoleBaseDeviation(size, zone);
		if (it->second.basis == "es")
		{
			double upper = base;
			if (it->second.mode == "increment" && grade <= 8)
			{
				double correction = 0.0;
				bool hasExplicitCorrection = false;

				std::map<int, std::vector<RangeValue> >::const_iterator correctionIt =
					it->second.corrections.find(grade);
				if (correctionIt != it->second.corrections.end())
				{
					hasExplicitCorrection = TryFindRangeValue(size, correctionIt->second, &correction);
				}

				if (!hasExplicitCorrection)
				{
					if (grade <= 5)
					{
						throw std::runtime_error("Increment hole zone requires grade above IT5");
					}

					const double previousIT = GetIT(size, grade - 1);
					correction = IT - previousIT;
				}

				upper += correction;
			}

			d.upper = upper;
			d.lower = base - IT;
			if (it->second.mode == "increment" && grade <= 8)
			{
				d.lower = d.upper - IT;
			}
			return d;
		}

		d.lower = base;
		d.upper = base + IT;
		return d;
	}

	Deviation ComputeShaft(double size, const std::string& zone, int grade)
	{
		const double IT = GetIT(size, grade);
		const ToleranceData& data = GetToleranceData();
		std::map<std::string, SymbolData>::const_iterator it = data.shaftSymbols.find(zone);
		if (it == data.shaftSymbols.end())
		{
			throw std::runtime_error("Unsupported shaft deviation zone: " + zone);
		}

		Deviation d;
		if (it->second.mode == "constant")
		{
			d.upper = 0;
			d.lower = -IT;
			return d;
		}

		if (it->second.mode == "symmetric")
		{
			const double halfTolerance = std::ceil(IT / 2.0);
			d.upper = halfTolerance;
			d.lower = -halfTolerance;
			return d;
		}

		const double base = GetShaftBaseDeviation(size, zone);
		if (it->second.basis == "ei")
		{
			d.lower = base;
			d.upper = base + IT;
			return d;
		}

		d.upper = base;
		d.lower = base - IT;
		return d;
	}
}

FitResult ComputeFit(double size, const std::string& fitStr)
{
	Fit fit = ParseFit(fitStr);

	FitResult result;
	result.hole = ComputeHole(size, fit.hole.zone, fit.hole.grade);
	result.shaft = ComputeShaft(size, fit.shaft.zone, fit.shaft.grade);

	return result;
}
