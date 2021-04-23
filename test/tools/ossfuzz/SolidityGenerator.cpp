/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0

#include <test/tools/ossfuzz/SolidityGenerator.h>

#include <libsolutil/Whiskers.h>
#include <libsolutil/Visitor.h>

#include <boost/algorithm/string/join.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm/copy.hpp>


using namespace solidity::test::fuzzer;
using namespace solidity::test::fuzzer::mutator;
using namespace solidity::util;
using namespace std;

GeneratorBase::GeneratorBase(std::shared_ptr<SolidityGenerator> _mutator)
{
	mutator = std::move(_mutator);
	state = mutator->testState();
	uRandDist = mutator->uniformRandomDist();
}

string GeneratorBase::visitChildren()
{
	ostringstream os;
	// Randomise visit order
	vector<std::pair<GeneratorPtr, unsigned>> randomisedChildren;
	for (auto const& child: generators)
		randomisedChildren.push_back(child);
	shuffle(randomisedChildren.begin(), randomisedChildren.end(), *uRandDist->randomEngine);
	for (auto const& child: randomisedChildren)
		if (uRandDist->likely(child.second + 1))
			for (unsigned i = 0; i < uRandDist->distributionOneToN(child.second); i++)
				os << std::visit(GenericVisitor{
					[&](auto const& _item) { return _item->generate(); }
				}, child.first);
	return os.str();
}

void SourceState::print(std::ostream& _os) const
{
	for (auto const& import: importedSources)
		_os << "Imports: " << import << std::endl;
}

set<string> TestState::sourceUnitPaths() const
{
	set<string> keys;
	boost::copy(sourceUnitState | boost::adaptors::map_keys, std::inserter(keys, keys.begin()));
	return keys;
}

string TestState::randomPath(set<string> const& _sourceUnitPaths) const
{
	auto it = _sourceUnitPaths.begin();
	/// Advance iterator by n where 0 <= n <= sourceUnitPaths.size() - 1
	size_t increment = uRandDist->distributionOneToN(_sourceUnitPaths.size()) - 1;
	solAssert(
		increment >= 0 && increment < _sourceUnitPaths.size(),
		"Solc custom mutator: Invalid increment"
	);
	advance(it, increment);
	return *it;
}

string TestState::randomPath() const
{
	solAssert(!empty(), "Solc custom mutator: Null test state");
	return randomPath(sourceUnitPaths());
}

void TestState::print(std::ostream& _os) const
{
	_os << "Printing test state" << std::endl;
	for (auto const& item: sourceUnitState)
	{
		_os << "Source path: " << item.first << std::endl;
		item.second->print(_os);
	}
}

string TestState::randomNonCurrentPath() const
{
	/// To obtain a source path that is not the currently visited
	/// source unit itself, we require at least one other source
	/// unit to be previously visited.
	solAssert(size() >= 2, "Solc custom mutator: Invalid test state");

	set<string> filteredSourcePaths;
	string currentPath = currentSourceUnitPath;
	set<string> sourcePaths = sourceUnitPaths();
	copy_if(
		sourcePaths.begin(),
		sourcePaths.end(),
		inserter(filteredSourcePaths, filteredSourcePaths.begin()),
		[currentPath](string const& _item) {
			return _item != currentPath;
		}
	);
	return randomPath(filteredSourcePaths);
}

void TestCaseGenerator::setup()
{
	addGenerators({
		{mutator->generator<SourceUnitGenerator>(), s_maxSourceUnits}
	});
}

string TestCaseGenerator::visit()
{
	return visitChildren();
}

void SourceUnitGenerator::setup()
{
	addGenerators({
		{mutator->generator<ImportGenerator>(), s_maxImports},
		{mutator->generator<PragmaGenerator>(), 1},
		{mutator->generator<ContractGenerator>(), 1},
		{mutator->generator<FunctionGenerator>(), s_maxFreeFunctions}
	});
}

string SourceUnitGenerator::visit()
{
	state->addSource();
	ostringstream os;
	os << "\n"
	   << "==== Source: "
	   << state->currentPath()
	   << " ===="
	   << "\n";
	os << visitChildren();
	return os.str();
}

string PragmaGenerator::visit()
{
	set<string> pragmas = uRandDist->subset(s_genericPragmas);
	// Choose either abicoder v1 or v2 but not both.
	pragmas.insert(s_abiPragmas[uRandDist->distributionOneToN(s_abiPragmas.size()) - 1]);
	return boost::algorithm::join(pragmas, "\n") + "\n";
}

string ImportGenerator::visit()
{
	/*
	 * Case 1: No source units defined
	 * Case 2: One source unit defined
	 * Case 3: At least two source units defined
	 */
	ostringstream os;
	string importPath;
	// Import a different source unit if at least
	// two source units available.
	if (state->size() > 1)
		importPath = state->randomNonCurrentPath();
	// Do not reimport already imported source unit
	if (!importPath.empty() && !state->sourceUnitState[state->currentPath()]->sourcePathImported(importPath))
	{
		os << "import "
		   << "\""
		   << importPath
		   << "\";\n";
		state->sourceUnitState[state->currentPath()]->addImportedSourcePath(importPath);
	}
	return os.str();
}

void ContractGenerator::setup()
{
	addGenerators({
		{mutator->generator<FunctionGenerator>(), s_maxFunctions}
	});
}

string ContractGenerator::visit()
{
	ScopeGuard reset([&]() {
		mutator->generator<FunctionGenerator>()->scope(true);
		state->unindent();
	});
	auto set = [&]() {
		state->indent();
		mutator->generator<FunctionGenerator>()->scope(false);
	};
	ostringstream os;
	string name = state->newContract();
	state->updateContract(name);
	os << "contract " << name << " {" << endl;
	set();
	os << visitChildren();
	os << "}" << endl;
	return os.str();
}

string FunctionGenerator::visit()
{
	string visibility;
	string name = state->newFunction();
	state->updateFunction(name);
	if (!m_freeFunction)
		visibility = "public";

	return indentation(state->indentationLevel) +
		"function " + name + "() " + visibility + " pure {}\n";
}

template <typename T>
shared_ptr<T> SolidityGenerator::generator()
{
	for (auto& g: m_generators)
		if (holds_alternative<shared_ptr<T>>(g))
			return get<shared_ptr<T>>(g);
	solAssert(false, "");
}

SolidityGenerator::SolidityGenerator(unsigned _seed)
{
	m_generators = {};
	m_urd = make_shared<UniformRandomDistribution>(make_unique<RandomEngine>(_seed));
	m_state = make_shared<TestState>(m_urd);
}

template <size_t I>
void SolidityGenerator::createGenerators()
{
	if constexpr (I < std::variant_size_v<Generator>)
	{
		createGenerator<std::variant_alternative_t<I, Generator>>();
		createGenerators<I + 1>();
	}
}

string SolidityGenerator::generateTestProgram()
{
	createGenerators();
	for (auto& g: m_generators)
		std::visit(GenericVisitor{
			[&](auto const& _item) { return _item->setup(); }
		}, g);
	string program = generator<TestCaseGenerator>()->generate();
	destroyGenerators();
	return program;
}
