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

#include <libsolidity/analysis/ControlFlowRevertPruner.h>

#include <libsolutil/Algorithms.h>

namespace solidity::frontend
{

void ControlFlowRevertPruner::run(ASTNode const& _astRoot)
{
	// Run first iteration without following recursive loops
	_astRoot.accept(*this);

	// In the second iteration we process anything left pending
	removeRevertingNodes();
}

FunctionDefinition const* ControlFlowRevertPruner::resolveCall(FunctionCall const& _functionCall, ContractDefinition const* _contract)
{
	auto const& functionType = dynamic_cast<FunctionType const&>(
		*_functionCall.expression().annotation().type
	);

	if (!functionType.hasDeclaration())
		return nullptr;

	auto const& unresolvedFunctionDefinition =
		dynamic_cast<FunctionDefinition const&>(functionType.declaration());

	if (auto const* memberAccess = dynamic_cast<MemberAccess const*>(&_functionCall.expression()))
	{
		if (*memberAccess->annotation().requiredLookup == VirtualLookup::Super)
		{
			if (auto const typeType = dynamic_cast<TypeType const*>(memberAccess->expression().annotation().type))
				if (auto const contractType = dynamic_cast<ContractType const*>(typeType->actualType()))
				{
					solAssert(contractType->isSuper(), "");
					ContractDefinition const* superContract = contractType->contractDefinition().superContract(*_contract);

					return &unresolvedFunctionDefinition.resolveVirtual(
						*_contract,
						superContract
					);
				}
		}
		else
		{
			solAssert(*memberAccess->annotation().requiredLookup == VirtualLookup::Static, "");
			return &unresolvedFunctionDefinition;
		}
	}
	else if (auto const* identifier = dynamic_cast<Identifier const*>(&_functionCall.expression()))
	{
		solAssert(*identifier->annotation().requiredLookup == VirtualLookup::Virtual, "");
		return &unresolvedFunctionDefinition.resolveVirtual(*_contract);
	}

	return &unresolvedFunctionDefinition;
}

void ControlFlowRevertPruner::removeRevertingNodes()
{
	std::set<FunctionContractPair> pending_functions = keys(m_functions);

	while (pending_functions.begin() != pending_functions.end())
	{
		auto functionContractPair = *pending_functions.begin();
		pending_functions.erase(functionContractPair);

		auto& revertState = m_functions[functionContractPair];
		auto const* function = std::get<Function>(functionContractPair);
		auto const* contract = std::get<Contract>(functionContractPair);

		auto previousState = revertState;
		revertState = RevertState::AllPathsRevert;

		FunctionFlow const& functionFlow = m_cfg.functionFlow(*function, contract);

		solidity::util::BreadthFirstSearch<CFGNode*>{{functionFlow.entry}}.run(
			[&](CFGNode* _node, auto&& _addChild) {
				if (_node == functionFlow.exit)
					revertState = RevertState::HasNonRevertingPath;

				for (auto const* functionCall: _node->functionCalls)
				{
					auto const* resolvedFunction = resolveCall(*functionCall, contract);

					if (resolvedFunction == nullptr)
						continue;

					auto result = m_functions.find(
						FunctionContractPair(
							resolvedFunction->isFree() ? nullptr : contract,
							resolvedFunction
					));

					if (
						result != m_functions.end() &&
						std::get<Function>(result->first) != function
					)
						if (result->second == RevertState::AllPathsRevert)
							_node->exits = {functionFlow.revert};
				}

				for (CFGNode* exit: _node->exits)
					_addChild(exit);
		});

		// Mark all functions depending on this one as modified again
		if (previousState != revertState)
			for (auto& pair: m_calls[function])
				if (std::get<Contract>(pair) == contract)
					pending_functions.insert(pair);
	}
}

void ControlFlowRevertPruner::collectCalls(FunctionDefinition const& _function, ContractDefinition const* _mostDerivedContract)
{
	FunctionFlow const& functionFlow = m_cfg.functionFlow(_function, _mostDerivedContract);

	FunctionContractPair pair(_mostDerivedContract, &_function);

	m_functions[pair] = RevertState::HasNonRevertingPath;

	solidity::util::BreadthFirstSearch<CFGNode*>{{functionFlow.entry}}.run(
		[&](CFGNode* _node, auto&& _addChild) {
			for (auto const* functionCall: _node->functionCalls)
				m_calls[resolveCall(*functionCall, _mostDerivedContract)].insert(pair);

			for (CFGNode* exit: _node->exits)
				_addChild(exit);
	});
}

}
