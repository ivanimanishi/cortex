//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2007-2009, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "boost/python.hpp"
#include "boost/format.hpp"
#include "boost/tokenizer.hpp"

#include "maya/MPxNode.h"
#include "maya/MPxLocatorNode.h"
#include "maya/MPxDeformerNode.h"
#include "maya/MPxObjectSet.h"
#include "maya/MPxFieldNode.h"
#include "maya/MPxSurfaceShape.h"
#include "maya/MPxComponentShape.h"
#include "maya/MPxImagePlane.h"
#include "maya/MFnTypedAttribute.h"
#include "maya/MFnNumericAttribute.h"
#include "maya/MFnDependencyNode.h"
#include "maya/MFnCompoundAttribute.h"
#include "maya/MObjectArray.h"
#include "maya/MPlugArray.h"
#include "maya/MDGModifier.h"
#include "maya/MNodeMessage.h"
#include "maya/MFnExpression.h"
#include "maya/MFnDagNode.h"
#include "maya/MFnPluginData.h"

#include "IECoreMaya/ParameterisedHolder.h"
#include "IECoreMaya/Parameter.h"
#include "IECoreMaya/PythonCmd.h"
#include "IECoreMaya/MayaTypeIds.h"
#include "IECoreMaya/ObjectData.h"

#include "IECore/MessageHandler.h"
#include "IECore/CompoundParameter.h"
#include "IECore/Object.h"
#include "IECore/MeshPrimitive.h"
#include "IECore/SimpleTypedData.h"
#include "IECore/ObjectVector.h"

#include <stdlib.h>

using namespace IECore;
using namespace IECoreMaya;
using namespace boost::python;
using namespace boost;
using namespace std;

template<typename B>
MObject ParameterisedHolder<B>::aParameterisedClassName;
template<typename B>
MObject ParameterisedHolder<B>::aParameterisedVersion;
template<typename B>
MObject ParameterisedHolder<B>::aParameterisedSearchPathEnvVar;
template<typename B>
MObject ParameterisedHolder<B>::aDynamicParameters;

template<typename B>
ParameterisedHolder<B>::PLCB::PLCB( ParameterisedHolder<B> *node) : m_node(node)
{
}

template<typename B>
void ParameterisedHolder<B>::PLCB::postLoad()
{
	assert(m_node);
	m_node->getParameterised();
}

template<typename B>
ParameterisedHolder<B>::ParameterisedHolder()
	:	m_parameterised( 0 ), m_failedToLoad( false )
{
	m_plcb = new PLCB( this );
}

template<typename B>
ParameterisedHolder<B>::~ParameterisedHolder()
{
}

template<typename B>
void ParameterisedHolder<B>::postConstructor()
{
	B::setExistWithoutInConnections(true);
	B::setExistWithoutOutConnections(true);
}

template<typename B>
MStatus ParameterisedHolder<B>::setDependentsDirty( const MPlug &plug, MPlugArray &plugArray )
{
	if( plug==aParameterisedClassName || plug==aParameterisedVersion || plug==aParameterisedSearchPathEnvVar )
	{
		// if the held class changes in any way then we ditch it so we're forced to reload
		// in getParameterised().
		m_parameterised = 0;
		m_failedToLoad = false;
	}
	return B::setDependentsDirty( plug, plugArray );
}

template<typename B>
MStatus ParameterisedHolder<B>::shouldSave( const MPlug &plug, bool &isSaving )
{
	/// Maya 8.5 crashes when saving a GenericAttribute (such as that
	/// created by the MeshParameterHandler) containing an "empty" mesh.
	/// This only applies to ASCII files, saving to binary works. Here
	/// we prevent Maya saving the value.

	isSaving = true;
	ParameterPtr parameter = plugParameter( plug );

	if ( parameter )
	{
		ObjectPtr value = parameter->getValue();

		if (! value )
		{
			isSaving = false;
			return MS::kSuccess;
		}

		MeshPrimitivePtr mesh = runTimeCast< MeshPrimitive >( value );
		if ( mesh )
		{
			MeshPrimitivePtr emptyMesh = new MeshPrimitive();
			if ( mesh->isEqualTo( emptyMesh ) )
			{
				isSaving = false;
				return MS::kSuccess;
			}
		}
	}

	return MS::kSuccess;
}

template<typename B>
void ParameterisedHolder<B>::copyInternalData( MPxNode *node )
{
	// take a full copy so we don't end up referring to dynamic parameters on the source node.
	ParameterisedHolder<B> *tNode = static_cast<ParameterisedHolder<B> *>( node );
	CompoundObjectPtr dp = tNode->getDynamicParameters();
	setDynamicParameters( dp->copy() );
}

template<typename B>
void *ParameterisedHolder<B>::creator()
{
	return new ParameterisedHolder<B>();
}

template<typename B>
MStatus ParameterisedHolder<B>::initialize()
{
	MStatus s;
	MFnTypedAttribute tAttr;
	MFnNumericAttribute nAttr;

	aParameterisedClassName = tAttr.create( "className", "clas", MFnData::kString );
	tAttr.setReadable(true);
	tAttr.setWritable(true);
	tAttr.setStorable(true);
	tAttr.setConnectable(false);
	tAttr.setHidden(true);

	s = B::addAttribute( aParameterisedClassName );
	assert(s);

	aParameterisedVersion = nAttr.create("version", "ver", MFnNumericData::kInt, 1, &s );
	assert(s);
	nAttr.setReadable(true);
	nAttr.setWritable(true);
	nAttr.setStorable(true);
	nAttr.setConnectable(false);
	nAttr.setHidden(true);


	s = B::addAttribute( aParameterisedVersion );
	assert(s);

	aParameterisedSearchPathEnvVar = tAttr.create("searchPathEnvVar", "spev", MFnData::kString );
	tAttr.setReadable(true);
	tAttr.setWritable(true);
	tAttr.setStorable(true);
	tAttr.setConnectable(false);
	tAttr.setHidden(true);

	s = B::addAttribute( aParameterisedSearchPathEnvVar );
	assert(s);

	aDynamicParameters = tAttr.create( "dynamicParameters", "dprm", ObjectData::id );
	tAttr.setKeyable( false );
	tAttr.setReadable( true );
	tAttr.setWritable( true );
	tAttr.setStorable( true );
	tAttr.setConnectable( false );
	tAttr.setHidden( true );

	s = B::addAttribute( aDynamicParameters );
	assert( s );

	return MS::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::setParameterised( IECore::RunTimeTypedPtr p )
{
	MPlug pClassName( B::thisMObject(), aParameterisedClassName );
	MPlug pVersion( B::thisMObject(), aParameterisedVersion );
	MPlug pSearchPathEnvVar( B::thisMObject(), aParameterisedSearchPathEnvVar );
	pClassName.setValue( "" );
	pVersion.setValue( 0 );
	pSearchPathEnvVar.setValue( "" );

	m_parameterised = p;
	m_failedToLoad = false;

	MStatus s = createAndRemoveAttributes();
	if( !s )
	{
		m_parameterised = 0;
		return s;
	}

	return MStatus::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::setParameterised( const std::string &className, int classVersion, const std::string &searchPathEnvVar )
{
	MPlug pClassName( B::thisMObject(), aParameterisedClassName );
	MPlug pVersion( B::thisMObject(), aParameterisedVersion );
	MPlug pSearchPathEnvVar( B::thisMObject(), aParameterisedSearchPathEnvVar );
	MStatus s = pClassName.setValue( className.c_str() );
	pVersion.setValue( classVersion );
	pSearchPathEnvVar.setValue( searchPathEnvVar.c_str() );

	m_parameterised = 0;
	m_failedToLoad = false;

	if( getParameterised() )
	{
		return MStatus::kSuccess;
	}

	return MStatus::kFailure;
}

template<typename B>
MStatus ParameterisedHolder<B>::updateParameterised()
{
	IECore::CompoundObjectPtr dynamicParameters = getDynamicParameters();
	MStatus s = createAndRemoveAttributes( dynamicParameters );
	setDynamicParameters( dynamicParameters );
	return s;
}

template<typename B>
IECore::RunTimeTypedPtr ParameterisedHolder<B>::getParameterised( std::string *classNameOut, int *classVersionOut, std::string *searchPathEnvVarOut )
{
	MPlug pClassName( B::thisMObject(), aParameterisedClassName );
	MPlug pVersion( B::thisMObject(), aParameterisedVersion );
	MPlug pSearchPathEnvVar( B::thisMObject(), aParameterisedSearchPathEnvVar );

	MString className;
	int version;
	MString searchPathEnvVar;

	MStatus s = pClassName.getValue( className );

	pVersion.getValue( version );
	pSearchPathEnvVar.getValue( searchPathEnvVar );

	if( !m_parameterised && !m_failedToLoad )
	{
		m_failedToLoad = true;
		if( className!="" )
		{
			m_parameterised = loadClass( className, version, searchPathEnvVar );
			if( m_parameterised )
			{
				// we'll only create and remove attributes if we loaded successfully.
				// this avoids the situation where the loading fails due to some
				// correctable error, but we've just deleted all the attributes with
				// all the settings and connections important to the user.
				addDynamicParameters();
				if( createAndRemoveAttributes() )
				{
					m_failedToLoad = false;
				}
			}
		}
	}

	// fill output parameters
	if( m_parameterised )
	{
		if( classNameOut )
		{
			*classNameOut = className.asChar();
		}
		if( classVersionOut )
		{
			*classVersionOut = version;
		}
		if( searchPathEnvVarOut )
		{
			*searchPathEnvVarOut = searchPathEnvVar.asChar();
		}
	}

	return m_parameterised;
}

template<typename B>
MStatus ParameterisedHolder<B>::setNodeValues()
{
	// to update the parameter->name map if necessary
	getParameterised();

	MFnDependencyNode fnDN( B::thisMObject() );

	ParameterToAttributeNameMap::const_iterator it;
	for( it=m_parametersToAttributeNames.begin(); it!=m_parametersToAttributeNames.end(); it++ )
	{
		MPlug p = fnDN.findPlug( it->second );
		if( p.isNull() )
		{
			return MStatus::kFailure;
		}
		try
		{
			MStatus s = Parameter::setValue( it->first, p );
			if( !s )
			{
				return s;
			}
		}
		catch( std::exception &e )
		{
			msg( Msg::Error, "ParameterisedHolder::setNodeValues", boost::format( "Caught exception while setting parameter value to attribute %s : %s" ) % p.name().asChar() % e.what());
			return MStatus::kFailure;
		}
		catch( ... )
		{
			msg( Msg::Error, "ParameterisedHolder::setNodeValues", boost::format( "Caught exception while setting parameter value to attribute %s." ) % p.name().asChar() );
			return MStatus::kFailure;
		}
	}
	return MStatus::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::setNodeValue( ParameterPtr pa )
{
	MPlug p = parameterPlug( pa );
	if( p.isNull() )
	{
		return MStatus::kFailure;
	}

	MStatus s = MS::kSuccess;

	try
	{
		s = IECoreMaya::Parameter::setValue( pa, p );
	}
	catch ( std::exception &e )
	{
		msg( Msg::Error, "ParameterisedHolder::setNodeValues", boost::format( "Caught exception while setting parameter value to attribute %s : %s" ) % p.name().asChar() % e.what());
		s = MS::kFailure;
	}
	catch (...)
	{
		msg( Msg::Error, "ParameterisedHolder::setNodeValues", boost::format( "Caught exception while setting parameter value to attribute %s" ) % p.name().asChar());
		s = MS::kFailure;
	}

	return s;
}

template<typename B>
MStatus ParameterisedHolder<B>::setParameterisedValues()
{
	// to update the parameter->name map if necessary
	getParameterised();

	MFnDependencyNode fnDN( B::thisMObject() );

	ParameterToAttributeNameMap::const_iterator it;
	for( it=m_parametersToAttributeNames.begin(); it!=m_parametersToAttributeNames.end(); it++ )
	{
		MPlug p = fnDN.findPlug( it->second );
		if( p.isNull() )
		{
			return MStatus::kFailure;
		}
		try
		{
			MStatus s = Parameter::setValue( p, it->first );
			if( !s )
			{
				return s;
			}
		}
		catch( std::exception &e )
		{
			msg( Msg::Error, "ParameterisedHolder::setParameterisedValues", boost::format( "Caught exception while setting parameter value from %s : %s" ) % p.name().asChar() % e.what());
			return MStatus::kFailure;
		}
		catch( ... )
		{
			msg( Msg::Error, "ParameterisedHolder::setParameterisedValues", boost::format( "Caught exception while setting parameter value from %s" ) % p.name().asChar() );
			return MStatus::kFailure;
		}
	}
	return MStatus::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::setParameterisedValue( ParameterPtr pa )
{
	MPlug p = parameterPlug( pa );
	if( p.isNull() )
	{
		return MStatus::kFailure;
	}

	MStatus s = MS::kSuccess;

	try
	{
		s = IECoreMaya::Parameter::setValue( p, pa );
	}
	catch ( std::exception &e )
	{
		msg( Msg::Error, "ParameterisedHolder::setParameterisedValues", boost::format( "Caught exception while setting parameter value from %s : %s" ) % p.name().asChar() % e.what());
		s = MS::kFailure;
	}
	catch (...)
	{
		msg( Msg::Error, "ParameterisedHolder::setParameterisedValues", boost::format( "Caught exception while setting parameter value from %s" ) % p.name().asChar());
		s = MS::kFailure;
	}

	return s;
}

template<typename B>
MPlug ParameterisedHolder<B>::parameterPlug( IECore::ConstParameterPtr parameter )
{
	// to update the parameter->name map if necessary
	getParameterised();

	ParameterToAttributeNameMap::const_iterator it = m_parametersToAttributeNames.find( const_pointer_cast<IECore::Parameter>( parameter ) );
	if( it==m_parametersToAttributeNames.end() )
	{
		return MPlug();
	}

	MFnDependencyNode fnDN( B::thisMObject() );
	return MPlug( B::thisMObject(), fnDN.attribute( it->second ) );
}

template<typename B>
IECore::ParameterPtr ParameterisedHolder<B>::plugParameter( const MPlug &plug )
{
	assert( ! plug.isNull() );

	// to update the parameter->name map if necessary
	getParameterised();
	AttributeNameToParameterMap::const_iterator it = m_attributeNamesToParameters.find( plug.partialName() );
	if( it==m_attributeNamesToParameters.end() )
	{
		return 0;
	}
	return it->second;
}

template<typename B>
IECore::RunTimeTypedPtr ParameterisedHolder<B>::loadClass( const MString &className, int classVersion, const MString &searchPathEnvVar )
{
	string toExecute = boost::str( format(
			"IECore.ClassLoader.defaultLoader( \"%s\" ).load( \"%s\", %d )()\n"
		) % searchPathEnvVar.asChar() % className.asChar() % classVersion
	);

	try
	{
		handle<> resultHandle( PyRun_String(
			toExecute.c_str(),
			Py_eval_input, PythonCmd::globalContext().ptr(),
			PythonCmd::globalContext().ptr() )
		);
		object result( resultHandle );
		return extract<RunTimeTypedPtr>( result )();
	}
	catch( error_already_set & )
	{
		MFnDependencyNode fnDN( B::thisMObject() );

		msg( Msg::Error, "ParameterisedHolder::loadClass",
			boost::format( "Unable to load class \"%s\" version %d into node %s." ) % className.asChar() % classVersion % fnDN.name().asChar());

		PyErr_Print();
	}
	catch( ... )
	{
		MFnDependencyNode fnDN( B::thisMObject() );

		msg( Msg::Error, "ParameterisedHolder::loadClass",
			boost::format( "Unable to load class \"%s\" version %d into node %s." ) % className.asChar() % classVersion % fnDN.name().asChar());
	}
	return 0;
}

template<typename B>
MStatus ParameterisedHolder<B>::createAndRemoveAttributes( IECore::CompoundObjectPtr dynamicParameterStorage  )
{
	m_attributeNamesToParameters.clear();
	m_parametersToAttributeNames.clear();

	MStatus s;
	if( m_parameterised )
	{
		ParameterisedInterface *parameterisedInterface = dynamic_cast<ParameterisedInterface *>( m_parameterised.get() );
		s = createAttributesWalk( parameterisedInterface->parameters(), "parm", dynamicParameterStorage );
		if( !s )
		{
			msg( Msg::Error, "ParameterisedHolder::createAndRemoveAttributes", boost::format( "Unable to create attributes to represent class." ) );
			return s;
		}
	}

	s = removeUnecessaryAttributes( dynamicParameterStorage );
	if( !s )
	{
		msg( Msg::Error, "ParameterisedHolder::createAndRemoveAttributes", "Failed to remove unecessary attributes." );
		return s;
	}

	return MS::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::createAttributesWalk( IECore::ConstCompoundParameterPtr parameter, const std::string &rootName, IECore::CompoundObjectPtr dynamicParameterStorage )
{
	MFnDependencyNode fnDN( B::thisMObject() );

	MString thisNodeName = B::name();
	MFnDagNode fnDAGN( B::thisMObject() );
	if( fnDAGN.hasObj( B::thisMObject() ) )
	{
		thisNodeName = fnDAGN.fullPathName();
	}

	const CompoundParameter::ParameterVector &children = parameter->orderedParameters();
	for( size_t i=0; i<children.size(); i++ )
	{
		string attributeName = rootName + "_" + children[i]->name();
		MString mAttributeName = attributeName.c_str();

		m_attributeNamesToParameters[mAttributeName] = children[i];
		m_parametersToAttributeNames[children[i]] = mAttributeName;

		MPlugArray connectionsFromMe, connectionsToMe;

		// try to reuse the old attribute if we can
		MObject attribute = fnDN.attribute( mAttributeName );
		MStatus s = MS::kFailure;
		if( !attribute.isNull() )
		{
			s = IECoreMaya::Parameter::update( children[i], attribute );

			if( !s )
			{
				// failed to update (parameter type probably changed).
				// remove the current attribute and fall through to the create
				// code

				MPlug plug( B::thisMObject(), attribute );
				plug.connectedTo( connectionsFromMe, false, true );
				plug.connectedTo( connectionsToMe, true, false );

				fnDN.removeAttribute( attribute );
			}
		}

		// create a new attribute if we failed to reuse one
		if( !s )
		{
			attribute = IECoreMaya::Parameter::create( children[i], mAttributeName );
			MStatus s = fnDN.addAttribute( attribute );
			if( !s )
			{
				return s;
			}

			// if it's a dynamic parameter then remember it for later
			if( dynamicParameterStorage )
			{

				IECore::ObjectVectorPtr ov = dynamicParameterStorage->member<IECore::ObjectVector>( rootName );
				if( !ov )
				{
					ov = new ObjectVector;
					dynamicParameterStorage->members()[rootName] = ov;
				}

				ov->members().push_back( children[i] );
			}

			// restore any existing connections
			if ( connectionsFromMe.length() || connectionsToMe.length() )
			{
				MDGModifier dgMod;
				MPlug plug( B::thisMObject(), attribute );
				for (unsigned i = 0; i < connectionsFromMe.length(); i++)
				{
					dgMod.connect( plug, connectionsFromMe[i] );
				}
				for (unsigned i = 0; i < connectionsToMe.length(); i++)
				{
					dgMod.connect( connectionsToMe[i], plug );
				}

				dgMod.doIt();
			}

			// make any connections requested in userData.
			// \todo I think this section should actually be performed by the parameter handlers themselves in the create() method.
			if( !attribute.isNull() )
			{
				MPlug plug( B::thisMObject(), attribute );
				connectionsToMe.clear();
				plug.connectedTo( connectionsToMe, true, false );

				if ( connectionsToMe.length() == 0 )
				{
					// creates default connections based on userData["maya"]["defaultConnection"] value...
					CompoundObject::ObjectMap &userData = children[i]->userData()->members();
					CompoundObject::ObjectMap::const_iterator it = userData.find( "maya" );
					if ( it != userData.end() && it->second->typeId() == CompoundObjectTypeId )
					{
						CompoundObject::ObjectMap &mayaUserData = static_pointer_cast<CompoundObject>(it->second)->members();
						it = mayaUserData.find( "defaultConnection" );
						if ( it != mayaUserData.end() && it->second->typeId() == StringDataTypeId )
						{
							std::string defaultConnection = static_pointer_cast<StringData>(it->second)->readable();
							std::string cmd = string( "connectAttr " ) + defaultConnection + " " + thisNodeName.asChar() + "." + plug.partialName().asChar();
							MDGModifier dgMod;
							dgMod.commandToExecute( cmd.c_str() );
							dgMod.doIt();
						}
						it = mayaUserData.find( "defaultExpression" );
						if ( it != mayaUserData.end() && it->second->typeId() == StringDataTypeId )
						{
							std::string defaultExpression = static_pointer_cast<StringData>(it->second)->readable();
							MString cmd = thisNodeName + "." + plug.partialName() + defaultExpression.c_str();
							MFnExpression expFn;
							expFn.create( cmd );
						}
					}
				}
			}

			/// Set the value of the attribute, in case it differs from the default
			MPlug plug( B::thisMObject(), attribute );
			s = IECoreMaya::Parameter::setValue( children[i], plug );
			if( !s )
			{
				return s;
			}
		}

		// recurse to the children if this is a compound child
		CompoundParameterPtr compoundChild = runTimeCast<CompoundParameter>( children[i] );
		if( compoundChild )
		{
			// in the case of a hierarchy of dynamic attributes, we don't want to store all the children in the dynamicParameterStorage - we
			// just want to store the root of the dynamic hierarchy. so if this compound parameter is already in the dynamic storage then we'll stop
			// storing dynamic parameters for the walk below this point.
			CompoundObjectPtr dynamicParameterStorageForWalk = dynamicParameterStorage;
			if( dynamicParameterStorageForWalk )
			{
				CompoundObject::ObjectMap::const_iterator it = dynamicParameterStorageForWalk->members().find( rootName );
				if( it!=dynamicParameterStorageForWalk->members().end() )
				{
					ConstObjectVectorPtr dpo = IECore::runTimeCast<const IECore::ObjectVector>( it->second );
					const ObjectVector::MemberContainer &dp = dpo->members();
					if( find( dp.begin(), dp.end(), compoundChild )!=dp.end() )
					{
						dynamicParameterStorageForWalk = 0;
					}
				}
			}

			MStatus s = createAttributesWalk( compoundChild, rootName + "_" + compoundChild->name(), dynamicParameterStorageForWalk );
			if( !s )
			{
				return s;
			}
		}
	}

	return MS::kSuccess;
}

template<typename B>
MStatus ParameterisedHolder<B>::removeUnecessaryAttributes( IECore::CompoundObjectPtr dynamicParameterStorage )
{
	/// \todo Make this a parameter, but only when the library's major version is incremented
	const std::string &rootName = "parm_";

	MObjectArray toRemove;
	MFnDependencyNode fnDN( B::thisMObject() );
	for( unsigned i=0; i<fnDN.attributeCount(); i++ )
	{
		MObject attr = fnDN.attribute( i );
		MFnAttribute fnAttr( attr );

		MString attrName = fnAttr.name();
		if( 0==strncmp( attrName.asChar(), rootName.c_str(), rootName.size() ) )
		{
			if( m_attributeNamesToParameters.find( fnAttr.name() )==m_attributeNamesToParameters.end() )
			{
				if( fnAttr.parent().isNull() )
				{
					toRemove.append( attr );
				}
				else
				{
					// we don't need to remove attributes which are the children
					// of compounds as they'll be removed when their parent is removed
				}
			}
		}
	}
	for( unsigned i=0; i<toRemove.length(); i++ )
	{
		MStatus s = fnDN.removeAttribute( toRemove[i] );
		if( !s )
		{
			return s;
		}
	}

	// remove dynamic parameters from our stash if they've been removed from the held object
	if( dynamicParameterStorage )
	{
		IECore::CompoundObject::ObjectMap &dynParms = dynamicParameterStorage->members();
		for( IECore::CompoundObject::ObjectMap::iterator pIt=dynParms.begin(); pIt!=dynParms.end(); )
		{
			ObjectVectorPtr parametersO = IECore::runTimeCast<IECore::ObjectVector>( pIt->second );
			if( !parametersO )
			{
				continue;
			}

			IECore::ObjectVector::MemberContainer &parameters = parametersO->members();
			for( IECore::ObjectVector::MemberContainer::iterator cIt=parameters.begin(); cIt!=parameters.end();  )
			{
				IECore::ParameterPtr parameter = IECore::runTimeCast<IECore::Parameter>( *cIt );
				if( parameter )
				{
					if( m_parametersToAttributeNames.find( parameter )==m_parametersToAttributeNames.end() )
					{
						cIt = parameters.erase( cIt );
						continue; // skip increment of cIt
					}
				}
				cIt++;
			}

			if( !parameters.size() )
			{
				IECore::CompoundObject::ObjectMap::iterator nIt = pIt; nIt++;
				dynParms.erase( pIt );
				pIt = nIt;
			}
			else
			{
				pIt++;
			}
		}
	}

	return MStatus::kSuccess;
}

template<typename B>
CompoundObjectPtr ParameterisedHolder<B>::getDynamicParameters()
{
	MPlug pDynamicParameters( B::thisMObject(), aDynamicParameters );
	MObject oDynamicParameters = pDynamicParameters.asMObject();
	MFnPluginData fnDynamicParameters( oDynamicParameters );
	ObjectData *oData = static_cast<ObjectData *>( fnDynamicParameters.data() );
	if( oData )
	{
		return IECore::runTimeCast<IECore::CompoundObject>( oData->getObject() );
	}
	else
	{
		return new IECore::CompoundObject;
	}
}

template<typename B>
void ParameterisedHolder<B>::setDynamicParameters( IECore::CompoundObjectPtr dynamicParameters )
{
	MPlug pDynamicParameters( B::thisMObject(), aDynamicParameters );
	MFnPluginData fnPD;
	MObject o = fnPD.create( ObjectData::id );
	ObjectData *oData = static_cast<ObjectData *>( fnPD.data() );
	oData->setObject( dynamicParameters );
	oData->setCopyMode( ObjectData::Shallow ); // so we keep the exact same parameter objects help by m_parameterised
	pDynamicParameters.setValue( o );
}

template<typename B>
void ParameterisedHolder<B>::addDynamicParameters()
{
	if( !m_parameterised )
	{
		return;
	}

	IECore::CompoundObjectPtr dynParmsO = getDynamicParameters();
	IECore::CompoundObject::ObjectMap &dynParms = dynParmsO->members();
	for( IECore::CompoundObject::ObjectMap::iterator it=dynParms.begin(); it!=dynParms.end(); it++ )
	{
		// find the parent parameter we should add to
		IECore::CompoundParameterPtr parentParameter = dynamic_cast<IECore::ParameterisedInterface *>( m_parameterised.get() )->parameters();

		typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;
		Tokenizer nameParts( it->first.value(), char_separator<char>( "_" ) );
		Tokenizer::const_iterator tIt = nameParts.begin();
		while( ++tIt!=nameParts.end() && parentParameter )
		{
			parentParameter = parentParameter->parameter<IECore::CompoundParameter>( *tIt );
			if( !parentParameter )
			{
				break;
			}
		}

		if( !parentParameter  )
		{
			IECore::msg( IECore::Msg::Warning, "ParameterisedHolder::addDynamicParameters", boost::format( "Unable to find parent parameter for dynamic parameters below \"%s\"." ) % it->first.value() );
			continue;
		}

		// add the parameters

		ObjectVectorPtr parametersO = IECore::runTimeCast<IECore::ObjectVector>( it->second );

		if( !parametersO )
		{
			IECore::msg( IECore::Msg::Warning, "ParameterisedHolder::addDynamicParameters", boost::format( "Unable to find any dynamic children for \"%s\"." ) % it->first.value() );
			continue;
		}

		IECore::ObjectVector::MemberContainer &parameters = parametersO->members();
		for( IECore::ObjectVector::MemberContainer::iterator it=parameters.begin(); it!=parameters.end(); it++ )
		{
			IECore::ParameterPtr parameter = IECore::runTimeCast<IECore::Parameter>( *it );
			if( parameter )
			{
				parentParameter->addParameter( parameter );
			}
		}
	}
}

// specialisations of the different typeIds
template<>
MTypeId ParameterisedHolderNode::id( ParameterisedHolderNodeId );

template<>
MString ParameterisedHolderNode::typeName( "ieParameterisedHolderNode" );

template<>
MTypeId ParameterisedHolderLocator::id( ParameterisedHolderLocatorId );

template<>
MString ParameterisedHolderLocator::typeName( "ieParameterisedHolderLocator" );

template<>
MTypeId ParameterisedHolderDeformer::id( ParameterisedHolderDeformerId );

template<>
MString ParameterisedHolderDeformer::typeName( "ieParameterisedHolderDeformer" );

template<>
MTypeId ParameterisedHolderField::id( ParameterisedHolderFieldId );

template<>
MString ParameterisedHolderField::typeName( "ieParameterisedHolderField" );

template<>
MTypeId ParameterisedHolderSet::id( ParameterisedHolderSetId );

template<>
MString ParameterisedHolderSet::typeName( "ieParameterisedHolderSet" );

template<>
MTypeId ParameterisedHolderSurfaceShape::id( ParameterisedHolderSurfaceShapeId );

template<>
MString ParameterisedHolderSurfaceShape::typeName( "ieParameterisedHolderSurfaceShape" );

template<>
MTypeId ParameterisedHolderComponentShape::id( ParameterisedHolderComponentShapeId );

template<>
MString ParameterisedHolderComponentShape::typeName( "ieParameterisedHolderComponentShape" );

template<>
MTypeId ParameterisedHolderImagePlane::id( ParameterisedHolderImagePlaneId );

template<>
MString ParameterisedHolderImagePlane::typeName( "ieParameterisedHolderImagePlane" );

// explicit instantiation
template class ParameterisedHolder<MPxNode>;
template class ParameterisedHolder<MPxLocatorNode>;
template class ParameterisedHolder<MPxDeformerNode>;
template class ParameterisedHolder<MPxFieldNode>;
template class ParameterisedHolder<MPxObjectSet>;
template class ParameterisedHolder<MPxSurfaceShape>;
template class ParameterisedHolder<MPxComponentShape>;
template class ParameterisedHolder<MPxImagePlane>;
