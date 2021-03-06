/*  ===========================================================================
*
*   This file is part of HISE.
*   Copyright 2016 Christoph Hart
*
*   HISE is free software: you can redistribute it and/or modify
*   it under the terms of the GNU General Public License as published by
*   the Free Software Foundation, either version 3 of the License, or
*   (at your option) any later version.
*
*   HISE is distributed in the hope that it will be useful,
*   but WITHOUT ANY WARRANTY; without even the implied warranty of
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*   GNU General Public License for more details.
*
*   You should have received a copy of the GNU General Public License
*   along with HISE.  If not, see <http://www.gnu.org/licenses/>.
*
*   Commercial licenses for using HISE in an closed source project are
*   available on request. Please visit the project's website to get more
*   information about commercial licensing:
*
*   http://www.hise.audio/
*
*   HISE is based on the JUCE library,
*   which also must be licenced for commercial applications:
*
*   http://www.juce.com
*
*   ===========================================================================
*/

// This file contains all methods that use forward declared inner classes


//==============================================================================

namespace hise { using namespace juce;

bool HiseJavascriptEngine::isJavascriptFunction(const var& v)
{
	return v.isObject() && dynamic_cast<HiseJavascriptEngine::RootObject::FunctionObject*>(v.getObject());
}

HiseJavascriptEngine::HiseJavascriptEngine(JavascriptProcessor *p) : maximumExecutionTime(15.0), root(new RootObject()), unneededScope(new DynamicObject())
{
	root->hiseSpecialData.setProcessor(p);

	registerNativeObject(RootObject::ObjectClass::getClassName(), new RootObject::ObjectClass());
	registerNativeObject(RootObject::ArrayClass::getClassName(), new RootObject::ArrayClass());
	registerNativeObject(RootObject::StringClass::getClassName(), new RootObject::StringClass());
	registerApiClass(new RootObject::MathClass());
	registerNativeObject(RootObject::JSONClass::getClassName(), new RootObject::JSONClass());
	registerNativeObject(RootObject::IntegerClass::getClassName(), new RootObject::IntegerClass());
}

HiseJavascriptEngine::RootObject::RootObject() :
hiseSpecialData(this)
{
	setMethod("exec", exec);
	setMethod("eval", eval);
	setMethod("trace", trace);
	setMethod("charToInt", charToInt);
	setMethod("parseInt", IntegerClass::parseInt);
	setMethod("typeof", typeof_internal);
}


#if JUCE_MSVC
#pragma warning (push)
#pragma warning (disable : 4702)
#endif


HiseJavascriptEngine::RootObject::Statement::ResultCode HiseJavascriptEngine::RootObject::LockStatement::perform(const Scope& /*s*/, var*) const
{
	if (RegisterName* r = dynamic_cast<RegisterName*>(lockedObj.get()))
	{
		currentLock = &r->rootRegister->getLock(r->indexInRegister);
		return ResultCode::ok;
	}
	else if (ConstReference* cr = dynamic_cast<ConstReference*>(lockedObj.get()))
	{
		var* constObj = cr->ns->constObjects.getVarPointerAt(cr->index);

		if (ApiClass* api = dynamic_cast<ApiClass*>(constObj->getObject()))
		{
			currentLock = &api->apiClassLock;
			return ResultCode::ok;
		}
		else
		{
			currentLock = nullptr;
			location.throwError("Can't lock this object");
			return Statement::ok;
		}
	}
	else
	{
		currentLock = nullptr;
		location.throwError("Can't lock this object");
		return Statement::ok;
	}
}

#if JUCE_MSVC
#pragma warning (pop)
#endif



void HiseJavascriptEngine::RootObject::ArraySubscript::cacheIndex(AssignableObject *instance, const Scope &s) const
{
	if (cachedIndex == -1)
	{
		if (dynamic_cast<LiteralValue*>(index.get()) != nullptr ||
			dynamic_cast<ConstReference*>(index.get()) != nullptr ||
			dynamic_cast<DotOperator*>(index.get()))
		{
			if (DotOperator* dot = dynamic_cast<DotOperator*>(index.get()))
			{
				if (dynamic_cast<ConstReference*>(dot->parent.get()) != nullptr)
				{
					if (ConstScriptingObject* cso = dynamic_cast<ConstScriptingObject*>(dot->parent->getResult(s).getObject()))
					{
						int constantIndex = cso->getConstantIndex(dot->child);
						var possibleIndex = cso->getConstantValue(constantIndex);
						if (possibleIndex.isInt() || possibleIndex.isInt64())
						{
							cachedIndex = (int)possibleIndex;
						}
						else location.throwError("[]- access only possible with int values");
					}
					else location.throwError("[]-access using dot operator only valid with const objects as parent");
				}
				else location.throwError("[]-access using dot operator only valid with const objects as parent");
			}
			else
			{
				const var i = index->getResult(s);
				cachedIndex = instance->getCachedIndex(i);

				if (cachedIndex == -1) location.throwError("Property " + i.toString() + " not found");
			}
		}
		else location.throwError("[]-access must be used with a literal or constant");
	}
}


var HiseJavascriptEngine::RootObject::FunctionCall::getResult(const Scope& s) const
{
	try
	{
		if (!initialised)
		{
			initialised = true;

			if (DotOperator* dot = dynamic_cast<DotOperator*> (object.get()))
			{
				parentIsConstReference = dynamic_cast<ConstReference*>(dot->parent.get()) != nullptr;

				if (parentIsConstReference)
				{
					constObject = dynamic_cast<ConstScriptingObject*>(dot->parent->getResult(s).getObject());

					if (constObject != nullptr)
					{
						constObject->getIndexAndNumArgsForFunction(dot->child, functionIndex, numArgs);
						isConstObjectApiFunction = true;

						CHECK_CONDITION_WITH_LOCATION(functionIndex != -1, "function not found");
						CHECK_CONDITION_WITH_LOCATION(numArgs == arguments.size(), "argument amount mismatch: " + String(arguments.size()) + ", Expected: " + String(numArgs));
					}
				}
			}
		}

		if (isConstObjectApiFunction)
		{
			var parameters[5];

			for (int i = 0; i < arguments.size(); i++)
			{
				parameters[i] = arguments[i]->getResult(s);
				HiseJavascriptEngine::checkValidParameter(i, parameters[i], location);
			}
				

			return constObject->callFunction(functionIndex, parameters, numArgs);
		}

		if (DotOperator* dot = dynamic_cast<DotOperator*> (object.get()))
		{
			var thisObject(dot->parent->getResult(s));

			if (ConstScriptingObject* c = dynamic_cast<ConstScriptingObject*>(thisObject.getObject()))
			{
				c->getIndexAndNumArgsForFunction(dot->child, functionIndex, numArgs);

				CHECK_CONDITION_WITH_LOCATION(functionIndex != -1, "function not found");
				CHECK_CONDITION_WITH_LOCATION(numArgs == arguments.size(), "argument amount mismatch: " + String(arguments.size()) + ", Expected: " + String(numArgs));

				var parameters[5];

				for (int i = 0; i < arguments.size(); i++)
					parameters[i] = arguments[i]->getResult(s);

				return c->callFunction(functionIndex, parameters, numArgs);
			}

			if (DynamicObject* dynObj = thisObject.getDynamicObject())
			{
				var property = dynObj->getProperty(dot->child);

				if (auto obj = dynamic_cast<InlineFunction::Object*>(property.getObject()))
				{
					var parameters[5];

					for (int i = 0; i < arguments.size(); i++)
						parameters[i] = arguments[i]->getResult(s);

					return obj->performDynamically(s, parameters, arguments.size());
				}
			}

			return invokeFunction(s, s.findFunctionCall(location, thisObject, dot->child), thisObject);
		}


		var r = object->getResult(s);

		if (auto obj = dynamic_cast<InlineFunction::Object*>(r.getObject()))
		{
			var parameters[5];

			for (int i = 0; i < arguments.size(); i++)
				parameters[i] = arguments[i]->getResult(s);

			return obj->performDynamically(s, parameters, arguments.size());
		}

		var function(r);
		return invokeFunction(s, function, var(s.scope));
	}
	catch (String& errorMessage)
	{
		throw Error::fromLocation(location, errorMessage);
	}
}

var HiseJavascriptEngine::RootObject::Scope::findFunctionCall(const CodeLocation& location, const var& targetObject, const Identifier& functionName) const
{
	if (DynamicObject* o = targetObject.getDynamicObject())
	{
		if (const var* prop = getPropertyPointer(o, functionName))
			return *prop;

		for (DynamicObject* p = o->getProperty(getPrototypeIdentifier()).getDynamicObject(); p != nullptr;
			p = p->getProperty(getPrototypeIdentifier()).getDynamicObject())
		{
			if (const var* prop = getPropertyPointer(p, functionName))
				return *prop;
		}

		// if there's a class with an overridden DynamicObject::hasMethod, this avoids an error
		if (o->hasMethod(functionName))
			return var();
	}

	if (targetObject.isString())
		if (var* m = findRootClassProperty(StringClass::getClassName(), functionName))
			return *m;

	if (targetObject.isArray())
		if (var* m = findRootClassProperty(ArrayClass::getClassName(), functionName))
			return *m;

	if (var* m = findRootClassProperty(ObjectClass::getClassName(), functionName))
		return *m;

	location.throwError("Unknown function '" + functionName.toString() + "'");
	return var();
}


var HiseJavascriptEngine::callExternalFunction(var function, const var::NativeFunctionArgs& args, Result* result /*= nullptr*/)
{
	var returnVal(var::undefined());

	static const Identifier thisIdent("this");

	try
	{
		prepareTimeout();
		if (result != nullptr) *result = Result::ok();

		RootObject::FunctionObject *fo = dynamic_cast<RootObject::FunctionObject*>(function.getObject());

		if (fo != nullptr)
		{
			return fo->invoke(RootObject::Scope(nullptr, root, root), args);;
		}
	}
	catch (String &error)
	{
		jassertfalse;
		if (result != nullptr) *result = Result::fail(error);
	}
	catch (RootObject::Error &e)
	{
		static const Identifier func("function");

		if (result != nullptr) *result = Result::fail(root->dumpCallStack(e, func));
	}
	catch (Breakpoint& bp)
	{
		bp.copyLocalScopeToRoot(*root);
		sendBreakpointMessage(bp.index);

		static const Identifier func("function");
		if (result != nullptr) *result = Result::fail(root->dumpCallStack(RootObject::Error::fromBreakpoint(bp), func));
	}

	return returnVal;
}

Array<Identifier> HiseJavascriptEngine::RootObject::HiseSpecialData::hiddenProperties;

bool HiseJavascriptEngine::RootObject::HiseSpecialData::initHiddenProperties = true;

HiseJavascriptEngine::RootObject::HiseSpecialData::HiseSpecialData(RootObject* root_) :
JavascriptNamespace("root"),
root(root_)
{
	if (initHiddenProperties)
	{
		hiddenProperties.addIfNotAlreadyThere(Identifier("exec"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("eval"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("trace"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("charToInt"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("parseInt"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("typeof"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Object"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Array"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("String"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Math"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("JSON"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Integer"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Content"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("SynthParameters"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Engine"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Synth"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Sampler"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("Globals"));
		hiddenProperties.addIfNotAlreadyThere(Identifier("include"));

		initHiddenProperties = false;
	}

	for (int i = 0; i < 32; i++)
	{
		callbackTimes[i] = 0.0;
	}
}

HiseJavascriptEngine::RootObject::HiseSpecialData::~HiseSpecialData()
{
	debugInformation.clear();
}

void HiseJavascriptEngine::RootObject::HiseSpecialData::clear()
{
	clearDebugInformation();
	apiClasses.clear();
	inlineFunctions.clear();
	constObjects.clear();
	callbackNEW.clear();
	globals = nullptr;
}

HiseJavascriptEngine::RootObject::Callback *HiseJavascriptEngine::RootObject::HiseSpecialData::getCallback(const Identifier &callbackId)
{
	for (int i = 0; i < callbackNEW.size(); i++)
	{
		if (callbackNEW[i]->getName() == callbackId)
		{
			return callbackNEW[i];
		}
	}

	return nullptr;
}


const HiseJavascriptEngine::RootObject::JavascriptNamespace* HiseJavascriptEngine::RootObject::HiseSpecialData::getNamespace(const Identifier &namespaceId) const
{
	return const_cast<HiseSpecialData*>(this)->getNamespace(namespaceId);
}

HiseJavascriptEngine::RootObject::JavascriptNamespace* HiseJavascriptEngine::RootObject::HiseSpecialData::getNamespace(const Identifier &namespaceId)
{
	static const Identifier r("root");

	if (namespaceId == r) return this;

	for (int i = 0; i < namespaces.size(); i++)
	{
		if (namespaces[i]->id == namespaceId)
		{
			return namespaces[i].get();
		}
	}

	return nullptr;
}




void HiseJavascriptEngine::RootObject::HiseSpecialData::createDebugInformation(DynamicObject *rootObject)
{
	ScopedLock sl(debugLock);

	debugInformation.clear();

	for (int i = 0; i < constObjects.size(); i++)
	{
		debugInformation.add(new FixedVarPointerInformation(constObjects.getVarPointerAt(i), constObjects.getName(i), Identifier(), DebugInformation::Type::Constant));
		debugInformation.getLast()->location = constLocations[i];
	}

	const int numRegisters = varRegister.getNumUsedRegisters();

	for (int i = 0; i < numRegisters; i++)
	{
		debugInformation.add(new FixedVarPointerInformation(varRegister.getVarPointer(i), varRegister.getRegisterId(i), Identifier(), DebugInformation::Type::RegisterVariable));
		debugInformation.getLast()->location = registerLocations[i];
	}
		

	DynamicObject *globalObject = rootObject->getProperty("Globals").getDynamicObject();

	for (int i = 0; i < globalObject->getProperties().size(); i++)
		debugInformation.add(new DynamicObjectDebugInformation(globalObject, globalObject->getProperties().getName(i), DebugInformation::Type::Globals));

	for (int i = 0; i < rootObject->getProperties().size(); i++)
	{
		const Identifier propertyId = rootObject->getProperties().getName(i);
		if (hiddenProperties.contains(propertyId)) continue;

		debugInformation.add(new DynamicObjectDebugInformation(rootObject, propertyId, DebugInformation::Type::Variables));
	}

	for (int i = 0; i < namespaces.size(); i++)
	{
		JavascriptNamespace* ns = namespaces[i];

		debugInformation.add(new DebugableObjectInformation(ns, ns->id, DebugInformation::Type::Namespace));

		const int numNamespaceObjects = ns->getNumDebugObjects();

		for (int j = 0; j < numNamespaceObjects; j++)
		{
			debugInformation.add(ns->createDebugInformation(j));
		}
	}

	for (int i = 0; i < inlineFunctions.size(); i++)
	{
		InlineFunction::Object *o = dynamic_cast<InlineFunction::Object*>(inlineFunctions.getUnchecked(i).get());

		auto inlineDebugInfo = new DebugableObjectInformation(o, o->name, DebugInformation::Type::InlineFunction);

		inlineDebugInfo->location.fileName = o->location.fileName;
		inlineDebugInfo->location.charNumber = o->location.charNumber;

		debugInformation.add(inlineDebugInfo);
	}

#if JUCE_IOS
#else
	for (int i = 0; i < externalCFunctions.size(); i++)
	{
		ExternalCFunction* cf = externalCFunctions[i];

		debugInformation.add(new DebugableObjectInformation(cf, cf->name, DebugInformation::Type::ExternalFunction));
	}
#endif

	for (int i = 0; i < callbackNEW.size(); i++)
	{
		if (!callbackNEW[i]->isDefined()) continue;

		debugInformation.add(new DebugableObjectInformation(callbackNEW[i], callbackNEW[i]->getName(), DebugInformation::Type::Callback));
	}
}


DebugInformation* HiseJavascriptEngine::RootObject::JavascriptNamespace::createDebugInformation(int index) const
{
	int prevLimit = 0;
	int upperLimit = varRegister.getNumUsedRegisters();

	if (index < upperLimit)
	{
		DebugInformation* di = new FixedVarPointerInformation(varRegister.getVarPointer(index), varRegister.getRegisterId(index), id, DebugInformation::Type::RegisterVariable);
		di->location = registerLocations[index];
		return di;
	}

	prevLimit = upperLimit;
	upperLimit += inlineFunctions.size();

	if (index < upperLimit)
	{
		const int inlineIndex = index - prevLimit;

		InlineFunction::Object *o = dynamic_cast<InlineFunction::Object*>(inlineFunctions.getUnchecked(inlineIndex).get());

		return new DebugableObjectInformation(o, o->name, DebugInformation::Type::InlineFunction, id);
	}

	prevLimit = upperLimit;
	upperLimit += constObjects.size();

	if (index < upperLimit)
	{
		const int constIndex = index - prevLimit;

		DebugInformation* di = new FixedVarPointerInformation(constObjects.getVarPointerAt(constIndex), constObjects.getName(constIndex), id, DebugInformation::Type::Constant);
		di->location = constLocations[constIndex];
	
		return di;
	}

	return nullptr;
}


DynamicObject* HiseJavascriptEngine::RootObject::HiseSpecialData::getInlineFunction(const Identifier &inlineFunctionId)
{
	const String idAsString = inlineFunctionId.toString();

	if (idAsString.contains("."))
	{
		Identifier ns(idAsString.upToFirstOccurrenceOf(".", false, false));
		Identifier fn(idAsString.fromFirstOccurrenceOf(".", false, false));

		JavascriptNamespace* n = getNamespace(Identifier(ns));

		if (n != nullptr)
		{
			for (int i = 0; i < n->inlineFunctions.size(); i++)
			{
				if (dynamic_cast<InlineFunction::Object*>(n->inlineFunctions[i].get())->name == fn)
				{
					return n->inlineFunctions[i].get();
				}
			}
		}
	}
	else
	{
		for (int i = 0; i < inlineFunctions.size(); i++)
		{
			if (dynamic_cast<InlineFunction::Object*>(inlineFunctions[i].get())->name == inlineFunctionId)
			{
				return inlineFunctions[i].get();
			}
		}
	}

	return nullptr;
}


void HiseJavascriptEngine::RootObject::HiseSpecialData::throwExistingDefinition(const Identifier &name, VariableStorageType type, CodeLocation &l)
{
	String typeName;

	switch (type)
	{
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Undeclared: typeName = "undeclared";
		break;
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::LocalScope: typeName = "local variable";
		break;
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::RootScope: typeName = "variable";
		break;
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Register: typeName = "register variable";
		break;
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::ConstVariables: typeName = "const variable";
		break;
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Globals: typeName = "global variable";
		break;
	default:
		break;
	}

	l.throwError("Identifier " + name.toString() + " is already defined as " + typeName);
}

void HiseJavascriptEngine::RootObject::HiseSpecialData::checkIfExistsInOtherStorage(VariableStorageType thisType, const Identifier &name, CodeLocation& l)
{
	VariableStorageType type = getExistingVariableStorage(name);

	switch (thisType)
	{
	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Undeclared:
		break;

	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::LocalScope:
		break;

	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::RootScope:

		if (type == VariableStorageType::ConstVariables || type == VariableStorageType::Globals || type == VariableStorageType::Register)
			throwExistingDefinition(name, type, l);
		break;

	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Register:

		if (type == VariableStorageType::ConstVariables || type == VariableStorageType::Globals || type == VariableStorageType::RootScope)
			throwExistingDefinition(name, type, l);
		break;

	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::ConstVariables:

		if (type == VariableStorageType::Register || type == VariableStorageType::Globals || type == VariableStorageType::RootScope)
			throwExistingDefinition(name, type, l);
		break;

	case HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType::Globals:

		if (type == VariableStorageType::RootScope || type == VariableStorageType::ConstVariables || type == VariableStorageType::Register)
			throwExistingDefinition(name, type, l);
		break;
	default:
		break;
	}
}

HiseJavascriptEngine::RootObject::HiseSpecialData::VariableStorageType HiseJavascriptEngine::RootObject::HiseSpecialData::getExistingVariableStorage(const Identifier &name)
{
	if (constObjects.contains(name)) return VariableStorageType::ConstVariables;
	else if (varRegister.getRegisterIndex(name) != -1) return VariableStorageType::Register;
	else if (globals->getProperties().contains(name)) return VariableStorageType::Globals;
	else if (root->getProperties().contains(name)) return VariableStorageType::RootScope;
	else return VariableStorageType::Undeclared;
}


int HiseJavascriptEngine::RootObject::HiseSpecialData::getExternalCIndex(const Identifier& functionId)
{
	for (int i = 0; i < externalCFunctions.size(); i++)
	{
		if (externalCFunctions[i]->name == functionId) return i;
	}

	return -1;
}


var HiseJavascriptEngine::executeInlineFunction(var inlineFunction, var* arguments, Result* result)
{
	auto f = static_cast<HiseJavascriptEngine::RootObject::InlineFunction::Object*>(inlineFunction.getObject());

	auto rootObj = getRootObject();
	auto s = HiseJavascriptEngine::RootObject::Scope(nullptr, static_cast<HiseJavascriptEngine::RootObject*>(rootObj), rootObj);

	try
	{
		prepareTimeout();
		if (result != nullptr) *result = Result::ok();
		return f->performDynamically(s, arguments, 2);
	}
	catch (String &error)
	{
		jassertfalse;
		*result = Result::fail(error);
	}
	catch (RootObject::Error &e)
	{
		if (result != nullptr) *result = Result::fail(root->dumpCallStack(e, f->name));

		f->cleanUpAfterExecution();
	}
	catch (Breakpoint& bp)
	{
		if(bp.localScope == nullptr)
			bp.localScope = f->createDynamicObjectForBreakpoint().getDynamicObject();

		bp.copyLocalScopeToRoot(*root);

		sendBreakpointMessage(bp.index);
		
		if (result != nullptr) *result = Result::fail(root->dumpCallStack(RootObject::Error::fromBreakpoint(bp), f->name));

		f->cleanUpAfterExecution();
	}

	return var();
}

var HiseJavascriptEngine::executeCallback(int callbackIndex, Result *result)
{
	RootObject::Callback *c = root->hiseSpecialData.callbackNEW[callbackIndex];

	// You need to register the callback correctly...
	jassert(c != nullptr);

	if (c != nullptr && c->isDefined())
	{
		try
		{
			prepareTimeout();

			var returnVal = c->perform(root);

			if (result != nullptr) *result = Result::ok();

			c->cleanLocalProperties();

			return returnVal;
		}
		catch (String &error)
		{
			jassertfalse;
			if (result != nullptr) *result = Result::fail(error);
		}
		catch (RootObject::Error &e)
		{
			if (result != nullptr) *result = Result::fail(root->dumpCallStack(e, c->getName()));
		}
		catch (Breakpoint& bp)
		{
			ScopedLock sl(getDebugLock());

			if(bp.localScope == nullptr)
				bp.localScope = c->createDynamicObjectForBreakpoint().getDynamicObject();

			bp.copyLocalScopeToRoot(*root);

			sendBreakpointMessage(bp.index);
			
			if (result != nullptr) *result = Result::fail(root->dumpCallStack(RootObject::Error::fromBreakpoint(bp), c->getName()));
		}
	}

	c->cleanLocalProperties();

	return var();
}

void HiseJavascriptEngine::RootObject::Callback::setStatements(BlockStatement *s) noexcept
{
	statements = s;
	isCallbackDefined = s->statements.size() != 0;
}


var HiseJavascriptEngine::RootObject::Callback::perform(RootObject *root)
{
	RootObject::Scope s(nullptr, root, root);

	var returnValue = var::undefined();

#if USE_BACKEND
	const double pre = Time::getMillisecondCounterHiRes();


	root->addToCallStack(callbackName, nullptr);



	statements->perform(s, &returnValue);

	root->removeFromCallStack(callbackName);

	const double post = Time::getMillisecondCounterHiRes();
	lastExecutionTime = post - pre;
#else
	statements->perform(s, &returnValue);
#endif

	return returnValue;
}

AttributedString DynamicObjectDebugInformation::getDescription() const
{
	return AttributedString();
}

void ScriptingObject::logErrorAndContinue(const String &errorMessage) const
{
#if USE_BACKEND
    
    auto mc = getScriptProcessor()->getMainController_();
    auto chain = const_cast<ModulatorSynthChain*>(mc->getMainSynthChain());
    
    debugError(chain, errorMessage);
    
#else
    ignoreUnused(errorMessage);
    DBG(errorMessage);
#endif
}
    
void ScriptingObject::reportScriptError(const String &errorMessage) const
{
#if USE_BACKEND
	throw errorMessage;
#else
	
#if JUCE_DEBUG
	DBG(errorMessage);
#else
	ignoreUnused(errorMessage);
#endif

#endif
}

} // namespace hise

