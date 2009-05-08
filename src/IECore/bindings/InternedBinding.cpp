//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2008-2009, Image Engine Design Inc. All rights reserved.
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

#include "IECore/Interned.h"
#include "IECore/bindings/InternedBinding.h"

using namespace std;
using namespace boost;
using namespace boost::python;

namespace IECore
{

struct InternedStringFromPython
{
	InternedStringFromPython()
	{
		converter::registry::push_back(
			&convertible,
			&construct,
			type_id<InternedString> ()
		);
	}

	static void *convertible( PyObject *obj_ptr )
	{
		if ( !PyString_Check( obj_ptr ) )
		{
			return 0;
		}
		return obj_ptr;
	}

	static void construct(
	        PyObject *obj_ptr,
	        converter::rvalue_from_python_stage1_data *data )
	{
		assert( obj_ptr );
		assert( PyString_Check( obj_ptr ) );

		void* storage = (( converter::rvalue_from_python_storage<InternedString>* ) data )->storage.bytes;
		new( storage ) InternedString( PyString_AsString( obj_ptr ) );
		data->convertible = storage;
	}
};

void bindInterned()
{

	class_<InternedString>( "InternedString", init<const std::string &>() )
		.def( init<InternedString>() )
		.def( "__str__", &InternedString::value, return_value_policy<copy_const_reference>() )
		.def( "value", &InternedString::value, return_value_policy<copy_const_reference>() )
		.def( self == self )
		.def( self != self )
		.def( "size", &InternedString::size ).staticmethod( "size" )
	;
	implicitly_convertible<InternedString, string>();

	InternedStringFromPython();

}

} // namespace IECore
