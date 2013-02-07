//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2013, Image Engine Design Inc. All rights reserved.
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

#include "boost/filesystem/operations.hpp"
#include "boost/filesystem/path.hpp"
#include "boost/tokenizer.hpp"

#include "OBJ/OBJ_Geometry.h"
#include "OBJ/OBJ_SubNet.h"
#include "PRM/PRM_ChoiceList.h"
#include "SOP/SOP_Node.h"

#include "IECoreHoudini/SceneCacheNode.h"

using namespace IECore;
using namespace IECoreHoudini;

//////////////////////////////////////////////////////////////////////////////////////////
// SceneCacheNode implementation
//////////////////////////////////////////////////////////////////////////////////////////

template<typename BaseType>
SceneCacheNode<BaseType>::SceneCacheNode( OP_Network *net, const char *name, OP_Operator *op ) : BaseType( net, name, op )
{
}

template<typename BaseType>
SceneCacheNode<BaseType>::~SceneCacheNode()
{
}

template<typename BaseType>
PRM_Name SceneCacheNode<BaseType>::pFile( "file", "File" );

template<typename BaseType>
PRM_Name SceneCacheNode<BaseType>::pReload( "reload", "Reload" );

template<typename BaseType>
PRM_Name SceneCacheNode<BaseType>::pRoot( "root", "Root" );

template<typename BaseType>
PRM_Name SceneCacheNode<BaseType>::pSpace( "space", "Space" );

template<typename BaseType>
PRM_Default SceneCacheNode<BaseType>::rootDefault( 0, "/" );

template<typename BaseType>
PRM_Default SceneCacheNode<BaseType>::spaceDefault( World );

static PRM_Name spaceNames[] = {
	PRM_Name( "0", "World" ),
	PRM_Name( "1", "Path" ),
	PRM_Name( "2", "Local" ),
	PRM_Name( "3", "Object" ),
	PRM_Name( 0 ) // sentinal
};

template<typename BaseType>
PRM_ChoiceList SceneCacheNode<BaseType>::rootMenu( PRM_CHOICELIST_REPLACE, &SceneCacheNode<BaseType>::buildRootMenu );

template<typename BaseType>
PRM_ChoiceList SceneCacheNode<BaseType>::spaceList( PRM_CHOICELIST_SINGLE, &spaceNames[0] );

template<typename BaseType>
PRM_Template SceneCacheNode<BaseType>::parameters[] = {
	PRM_Template( PRM_FILE | PRM_TYPE_JOIN_NEXT, 1, &pFile ),
	PRM_Template(
		PRM_CALLBACK, 1, &pReload, 0, 0, 0, &SceneCacheNode<BaseType>::reloadButtonCallback, 0, 0,
		"Removes the current MDC file from the cache. This will force a recook on this node, and "
		"cause all other nodes using this MDC file to require a recook as well."
	),
	PRM_Template(
		PRM_STRING, 1, &pRoot, &rootDefault, &rootMenu, 0, 0, 0, 0,
		"Root path inside the MDC of the hierarchy to load"
	),
	PRM_Template(
		PRM_INT, 1, &pSpace, &spaceDefault, &spaceList, 0, 0, 0, 0,
		"Re-orient the objects by choosing a space. World transforms from \"/\" on down the hierarchy, "
		"Path re-roots the transformation starting at the specified root path, Local uses the current level "
		"transformations only, and Object is an identity transform"
	),
	PRM_Template()
};

template<typename BaseType>
void SceneCacheNode<BaseType>::buildRootMenu( void *data, PRM_Name *menu, int maxSize, const PRM_SpareData *, const PRM_Parm * )
{
	SceneCacheNode<BaseType> *node = reinterpret_cast<SceneCacheNode<BaseType>*>( data );
	if ( !node )
	{
		return;
	}
	
	menu[0].setToken( "/" );
	menu[0].setLabel( "/" );
	
	std::string file;
	if ( !node->ensureFile( file ) )
	{
		// mark the end of our menu
		menu[1].setToken( 0 );
		return;
	}
	
	std::vector<std::string> descendants;
	SceneCacheUtil::Cache::EntryPtr entry = cache().entry( file, "/" );
	node->descendantNames( entry->sceneCache(), descendants );
	node->createMenu( menu, descendants );
}

template<typename BaseType>
int SceneCacheNode<BaseType>::reloadButtonCallback( void *data, int index, float time, const PRM_Template *tplate )
{
	std::string file;
	SceneCacheNode<BaseType> *node = reinterpret_cast<SceneCacheNode<BaseType>*>( data );
	if ( !node || !node->ensureFile( file ) )
	{
		return 0;
	}
	
	cache().erase( file );
	node->forceRecook();
	
	return 1;
}

template<typename BaseType>
bool SceneCacheNode<BaseType>::ensureFile( std::string &file )
{
	file = getFile();
	
	boost::filesystem::path filePath = boost::filesystem::path( file );
	if ( filePath.extension() == ".mdc" && boost::filesystem::exists( filePath ) )
	{
		return true;
	}
	
	return false;
}

template<typename BaseType>
std::string SceneCacheNode<BaseType>::getFile()
{
	UT_String value;
	this->evalString( value, pFile.getToken(), 0, 0 );
	return ( value == "" ) ? "/" : value.toStdString();
}

template<typename BaseType>
void SceneCacheNode<BaseType>::setFile( std::string file )
{
	this->setString( UT_String( file ), CH_STRING_LITERAL, pFile.getToken(), 0, 0 );
}

template<typename BaseType>
std::string SceneCacheNode<BaseType>::getPath()
{
	UT_String value;
	this->evalString( value, pRoot.getToken(), 0, 0 );
	return ( value == "" ) ? "/" : value.toStdString();
}

template<typename BaseType>
void SceneCacheNode<BaseType>::setPath( std::string path )
{
	this->setString( UT_String( path ), CH_STRING_LITERAL, pRoot.getToken(), 0, 0 );
}

template<typename BaseType>
typename SceneCacheNode<BaseType>::Space SceneCacheNode<BaseType>::getSpace()
{
	return (Space)this->evalInt( pSpace.getToken(), 0, 0 );
}

template<typename BaseType>
void SceneCacheNode<BaseType>::setSpace( SceneCacheNode<BaseType>::Space space )
{
	this->setInt( pSpace.getToken(), 0, 0, space );
}

template<typename BaseType>
void SceneCacheNode<BaseType>::descendantNames( const IECore::SceneCache *cache, std::vector<std::string> &descendants )
{
	IndexedIO::EntryIDList children;
	cache->childNames( children );
	
	std::string current = ( cache->path() == "/" ) ? "" : cache->path();
	for ( IndexedIO::EntryIDList::const_iterator it=children.begin(); it != children.end(); ++it )
	{
		descendants.push_back( current + "/" + it->value() );
	}
	
	for ( IndexedIO::EntryIDList::const_iterator it=children.begin(); it != children.end(); ++it )
	{
		descendantNames( cache->readableChild( *it ), descendants );
	}
};

template<typename BaseType>
void SceneCacheNode<BaseType>::objectNames( const IECore::SceneCache *cache, std::vector<std::string> &objects )
{
	if ( cache->hasObject() )
	{
		objects.push_back( cache->name() );
	}
	
	IndexedIO::EntryIDList children;
	cache->childNames( children );
	for ( IndexedIO::EntryIDList::const_iterator it=children.begin(); it != children.end(); ++it )
	{
		objectNames( cache->readableChild( *it ), objects );
	}
};

template<typename BaseType>
void SceneCacheNode<BaseType>::createMenu( PRM_Name *menu, const std::vector<std::string> &values )
{
	unsigned pos = 1;
	// currently menus display funny if we exceed 1500 despite the limit being 8191...
	for ( std::vector<std::string>::const_iterator it=values.begin(); ( it != values.end() ) && ( pos < 1500 ); ++it, ++pos )
	{
		menu[pos].setToken( (*it).c_str() );
		menu[pos].setLabel( (*it).c_str() );
	}
	
	// mark the end of our menu
	menu[pos].setToken( 0 );
}

template<typename BaseType>
SceneCacheUtil::Cache &SceneCacheNode<BaseType>::cache()
{
	static SceneCacheUtil::Cache c;
	return c;
}

//////////////////////////////////////////////////////////////////////////////////////////
// SceneCacheUtil Cache implementation
//////////////////////////////////////////////////////////////////////////////////////////

SceneCacheUtil::Cache::Cache() : m_fileCache( fileCacheGetter, 200 )
{
};

SceneCacheUtil::Cache::EntryPtr SceneCacheUtil::Cache::entry( const std::string &fileName, const std::string &path )
{
	FileAndMutexPtr f = m_fileCache.get( fileName );
	EntryPtr result = new Entry( f ); // this locks the mutex for us
	result->m_entry = result->m_fileAndMutex->file;

	typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;
	Tokenizer tokens( path, boost::char_separator<char>( "/" ) );
	for ( Tokenizer::iterator tIt=tokens.begin(); tIt!=tokens.end(); ++tIt )
	{
		/// \todo: this will throw if the path is bad. how should we handle it?
		result->m_entry = result->m_entry->readableChild( *tIt );	
	}

	return result;
}

Imath::M44d SceneCacheUtil::Cache::worldTransform( const std::string &fileName, const std::string &path )
{
	EntryPtr thisEntry = entry( fileName, "/" );
	ConstSceneCachePtr cache = thisEntry->sceneCache();
	Imath::M44d result = cache->readTransform();
	typedef boost::tokenizer<boost::char_separator<char> > Tokenizer;
	Tokenizer tokens( path, boost::char_separator<char>( "/" ) );
	for ( Tokenizer::iterator tIt=tokens.begin(); tIt!=tokens.end(); ++tIt )
	{
		cache = cache->readableChild( *tIt );
		result = cache->readTransform() * result;
	}

	return result;
}

void SceneCacheUtil::Cache::erase( const std::string &fileName )
{
	m_fileCache.erase( fileName );
}

SceneCacheUtil::Cache::FileAndMutexPtr SceneCacheUtil::Cache::fileCacheGetter( const std::string &fileName, size_t &cost )
{
	FileAndMutexPtr result = new FileAndMutex;
	result->file = new SceneCache( fileName, IndexedIO::Read );
	cost = 1;
	return result;
}

//////////////////////////////////////////////////////////////////////////////////////////
// SceneCacheUtil Entry implementation
//////////////////////////////////////////////////////////////////////////////////////////

SceneCacheUtil::Cache::Entry::Entry( FileAndMutexPtr fileAndMutex )
	: m_fileAndMutex( fileAndMutex ), m_lock( m_fileAndMutex->mutex )
{
}

const SceneCache *SceneCacheUtil::Cache::Entry::sceneCache()
{
	return m_entry;
}

//////////////////////////////////////////////////////////////////////////////////////////
// Known Specializations
//////////////////////////////////////////////////////////////////////////////////////////

template class SceneCacheNode<OP_Node>;
template class SceneCacheNode<OBJ_Node>;
template class SceneCacheNode<OBJ_Geometry>;
template class SceneCacheNode<OBJ_SubNet>;
template class SceneCacheNode<SOP_Node>;
