//
// MySQLException.cpp
//
// $Id: //poco/1.3/Data/MySQL/src/ResultMetadata.cpp#5 $
//
// Library: Data
// Package: MySQL
// Module:  ResultMetadata
//
// Copyright (c) 2008, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Data/MySQL/ResultMetadata.h"
#include "Poco/Data/MySQL/MySQLException.h"
#include <cstring>


namespace
{
	class ResultMetadataHandle
		/// Simple exception-safe wrapper
	{
	public:

		explicit ResultMetadataHandle(MYSQL_STMT* stmt)
		{
			h = mysql_stmt_result_metadata(stmt);
		}

		~ResultMetadataHandle()
		{
			if (h)
			{
				mysql_free_result(h);
			}
		}

		operator MYSQL_RES* ()
		{
			return h;
		}

	private:

		MYSQL_RES* h;
	};

	size_t fieldSize(const MYSQL_FIELD& field)
		/// Convert field MySQL-type and field MySQL-length to actual field length
	{
		switch (field.type)
		{
		case MYSQL_TYPE_TINY:     return sizeof(char);
		case MYSQL_TYPE_SHORT:    return sizeof(short);
		case MYSQL_TYPE_INT24:
		case MYSQL_TYPE_LONG:     return sizeof(Poco::Int32);
		case MYSQL_TYPE_FLOAT:    return sizeof(float);
		case MYSQL_TYPE_DOUBLE:   return sizeof(double);
		case MYSQL_TYPE_LONGLONG: return sizeof(Poco::Int64);

		case MYSQL_TYPE_DECIMAL:
		case MYSQL_TYPE_NEWDECIMAL:
		case MYSQL_TYPE_STRING:
		case MYSQL_TYPE_VAR_STRING:
		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
		case MYSQL_TYPE_BLOB:
		case MYSQL_TYPE_NULL:
		case MYSQL_TYPE_TIMESTAMP:
		case MYSQL_TYPE_DATE:
		case MYSQL_TYPE_TIME:
		case MYSQL_TYPE_DATETIME:
		case MYSQL_TYPE_YEAR:
		case MYSQL_TYPE_NEWDATE:
		case MYSQL_TYPE_VARCHAR:
		case MYSQL_TYPE_BIT:
		case MYSQL_TYPE_ENUM:
		case MYSQL_TYPE_SET:
		case MYSQL_TYPE_GEOMETRY:
		default:
			return field.length;
		}

		throw Poco::Data::MySQL::StatementException("unknown field type");
	}	

	Poco::Data::MetaColumn::ColumnDataType fieldType(const MYSQL_FIELD& field)
		/// Convert field MySQL-type to Poco-type	
	{
		bool unsig = ((field.flags & UNSIGNED_FLAG) == UNSIGNED_FLAG);

		switch (field.type)
		{
		case MYSQL_TYPE_TINY:     
			if (unsig) return Poco::Data::MetaColumn::FDT_UINT8;
			return Poco::Data::MetaColumn::FDT_INT8;

		case MYSQL_TYPE_SHORT:
			if (unsig) return Poco::Data::MetaColumn::FDT_UINT16;
			return Poco::Data::MetaColumn::FDT_INT16;

		case MYSQL_TYPE_INT24:
		case MYSQL_TYPE_LONG:     
			if (unsig) return Poco::Data::MetaColumn::FDT_UINT32;
			return Poco::Data::MetaColumn::FDT_INT32;

		case MYSQL_TYPE_FLOAT:    
			return Poco::Data::MetaColumn::FDT_FLOAT;

		case MYSQL_TYPE_DOUBLE:   
			return Poco::Data::MetaColumn::FDT_DOUBLE;

		case MYSQL_TYPE_LONGLONG: 
			if (unsig) return Poco::Data::MetaColumn::FDT_UINT64;
			return Poco::Data::MetaColumn::FDT_INT64;

		case MYSQL_TYPE_STRING:
		case MYSQL_TYPE_VAR_STRING:
			return Poco::Data::MetaColumn::FDT_STRING;

		case MYSQL_TYPE_TINY_BLOB:
		case MYSQL_TYPE_MEDIUM_BLOB:
		case MYSQL_TYPE_LONG_BLOB:
		case MYSQL_TYPE_BLOB:
			return Poco::Data::MetaColumn::FDT_BLOB;

		case MYSQL_TYPE_DECIMAL:
		case MYSQL_TYPE_NEWDECIMAL:
		case MYSQL_TYPE_NULL:
		case MYSQL_TYPE_TIMESTAMP:
		case MYSQL_TYPE_DATE:
		case MYSQL_TYPE_TIME:
		case MYSQL_TYPE_DATETIME:
		case MYSQL_TYPE_YEAR:
		case MYSQL_TYPE_NEWDATE:
		case MYSQL_TYPE_VARCHAR:
		case MYSQL_TYPE_BIT:
		case MYSQL_TYPE_ENUM:
		case MYSQL_TYPE_SET:
		case MYSQL_TYPE_GEOMETRY:
		default:
			return Poco::Data::MetaColumn::FDT_UNKNOWN;
		}

		return Poco::Data::MetaColumn::FDT_UNKNOWN;
	}	
} // namespace


namespace Poco {
namespace Data {
namespace MySQL {


void ResultMetadata::reset()
{
	_columns.resize(0);
	_row.resize(0);
	_buffer.resize(0);
	_lengths.resize(0);
    _isNull.resize(0);
}


void ResultMetadata::init(MYSQL_STMT* stmt)
{
	ResultMetadataHandle h(stmt);

	if (!h)
	{
		// all right, it is normal
		// querys such an "INSERT INTO" just does not have result at all
		reset();
		return;
	}

	size_t count = mysql_num_fields(h);
	MYSQL_FIELD* fields = mysql_fetch_fields(h);

	size_t commonSize = 0;
	_columns.reserve(count);

	for (size_t i = 0; i < count; i++)
	{
		_columns.push_back(MetaColumn(
			i,                               // position
			fields[i].name,                  // name
			fieldType(fields[i]),            // type
			fieldSize(fields[i]),            // length
			0,                               // TODO: precision (Now I dont know how to get it)
			!IS_NOT_NULL(fields[i].flags)    // nullable
			));

		commonSize += _columns[i].length();
	}

	_buffer.resize(commonSize);
	_row.resize(count);
	_lengths.resize(count);
    _isNull.resize(count);

	size_t offset = 0;

	for (size_t i = 0; i < count; i++)
	{
		std::memset(&_row[i], 0, sizeof(MYSQL_BIND));

		_row[i].buffer_type   = fields[i].type;
		_row[i].buffer_length = static_cast<unsigned int>(_columns[i].length());
		_row[i].buffer        = &_buffer[0] + offset;
		_row[i].length        = &_lengths[i];
        _row[i].is_null       = &_isNull[i];
		
		offset += _row[i].buffer_length;
	}
}


Poco::UInt32 ResultMetadata::columnsReturned() const
{
	return static_cast<Poco::UInt32>(_columns.size());
}


const MetaColumn& ResultMetadata::metaColumn(Poco::UInt32 pos) const
{
	return _columns[pos];
}


MYSQL_BIND* ResultMetadata::row()
{
	return &_row[0];
}


size_t ResultMetadata::length(size_t pos) const
{
	return _lengths[pos];
}


const char* ResultMetadata::rawData(size_t pos) const 
{
	return reinterpret_cast<const char*>(_row[pos].buffer);
}


bool ResultMetadata::isNull(size_t pos) const 
{
    return (_isNull[pos] != 0);
}


} } } // namespace Poco::Data::MySQL
