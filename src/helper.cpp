#include "stdafx.h"
#include "helper.h"


class mainThreadTask : public main_thread_callback
{
	public:
		virtual void callback_run()
		{
			static_api_ptr_t < library_manager >m;
			switch( task_sel )
			{
				case 0:
				{
					is_library_enabled.set_value( m->is_library_enabled() );
					break;
				}

				case 1:
				{
					m->get_all_items( list );
					list_ptr.set_value( &list );
					break;
				}

				default:
				{
					console::printf( CONSOLE_HEADER"Invalid task_sel: %d" , task_sel );
					return;
				}
			};

			task_sel = -1;
			return;
		}


		int task_sel = -1;

		std::promise<bool> is_library_enabled;

		dbList list;
		std::promise< dbList * > list_ptr;
};


void open_helper( const char *p_path , const service_ptr_t<file> &p_file , playlist_loader_callback::ptr p_callback , abort_callback &p_abort )
{
	// load file
	pfc::string8 in_file;
	try
	{
		p_file->seek( 0 , p_abort );  // required, said SDK
		p_file->read_string_raw( in_file , p_abort );
	}
	catch( ... )
	{
		console::printf( CONSOLE_HEADER"exception from seek(), read_string_raw()" );
		throw;
	}

	tinyxml2::XMLDocument x;
	auto ret = x.Parse( in_file );
	if( ret != tinyxml2::XML_NO_ERROR )
	{
		console::printf( CONSOLE_HEADER"XML parse error id: %d, msg: %s" , ret , x.GetErrorStr1() );
		throw exception_io_data();
	}

	xmlBaseImpl xml_base;

	// 4.1.1 playlist
	const auto *x_playlist = x.FirstChildElement( "playlist" );
	if( x_playlist == nullptr )
	{
		console::printf( CONSOLE_HEADER"missing playlist element!" );
		throw exception_io_data();
	}
	// playlist xml:base
	const char *x_playlist_base = x_playlist->Attribute( "xml:base" );
	xml_base.setXmlBase( 0 , x_playlist_base );

	// 4.1.1.1.1 xmlns
	const char *x_playlist_ns = x_playlist->Attribute( "xmlns" );
	if( x_playlist_ns == nullptr )
	{
		console::printf( CONSOLE_HEADER"missing xmlns attribute!" );
		throw exception_io_data();
	}
	const pfc::string8 ns = "http://xspf.org/ns/0/";
	const int x_playlist_ns_eq = strncmp( x_playlist_ns , ns , ns.get_length() );
	if( x_playlist_ns_eq != 0 )
	{
		console::printf( CONSOLE_HEADER"namespace error: %s" , x_playlist_ns );
		throw exception_io_data();
	}

	// 4.1.1.1.2 version
	int x_playlist_version = -1;
	x_playlist->QueryIntAttribute( "version" , &x_playlist_version );
	if( ( x_playlist_version < 0 ) || ( x_playlist_version > 1 ) )
	{
		console::printf( CONSOLE_HEADER"version error: %d" , x_playlist_version );
		throw exception_io_data();
	}

	// 4.1.1.2.14 trackList
	const auto *x_tracklist = x_playlist->FirstChildElement( "trackList" );
	if( x_tracklist == nullptr )
	{
		console::printf( CONSOLE_HEADER"missing trackList element!" );
		throw exception_io_data();
	}
	// trackList xml:base
	const char *x_tracklist_base = x_tracklist->Attribute( "xml:base" );
	xml_base.setXmlBase( 1 , x_tracklist_base );

	// 4.1.1.2.14.1.1 track
	t_size counter = 0;
	dbList db_list;  // don't call main thread for every <track>
	lruCacheImpl lru_cache;
	for( auto *x_track = x_tracklist->FirstChildElement( "track" ) ; x_track != nullptr ; x_track = x_track->NextSiblingElement( "track" ) )
	{
		if( p_abort.is_aborting() )
			return;

		// track xml:base
		const char *x_track_base = x_track->Attribute( "xml:base" );
		xml_base.setXmlBase( 2 , x_track_base );

		// 4.1.1.2.14.1.1.1.1 location
		const auto *track_location = x_track->FirstChildElement( "location" );
		if( ( track_location != nullptr ) && ( track_location->GetText() != nullptr ) )
		{
			// have location
			open_helper_location( p_path , p_callback , x_track , &xml_base );
		}
		else
		{
			p_callback->on_progress( ( "track " + std::to_string( counter++ ) ).c_str() );

			if( db_list.get_count() == 0 )  // minimize async calls
			{
				// first time init

				// library_manager class could only be used in main thread, here is worker thread
				static_api_ptr_t<main_thread_callback_manager>m;
				service_ptr_t<mainThreadTask> m_task( new service_impl_t<mainThreadTask>() );

				// get library status
				m_task->task_sel = 0;
				auto is_library = m_task->is_library_enabled.get_future();

				m->add_callback( m_task );
				if( !is_library.get() )
				{
					console::printf( CONSOLE_HEADER"Media library is not enabled, please configure it first" );
					return;
				}

				// get media library
				m_task->task_sel = 1;
				auto list_ptr = m_task->list_ptr.get_future();

				m->add_callback( m_task );
				db_list.move_from( *( list_ptr.get() ) );
			}

			open_helper_no_location( p_callback , x_track , &db_list , &lru_cache );
		}
	}

	return;
}

void open_helper_location( const char *p_path , playlist_loader_callback::ptr p_callback , const tinyxml2::XMLElement *x_track , xmlBaseImpl *xml_base )
{
	const auto *track_location = x_track->FirstChildElement( "location" );

	// location xml:base
	const char *track_location_base = track_location->Attribute( "xml:base" );
	xml_base->setXmlBase( 3 , track_location_base );

	// file info variables
	file_info_impl f_info;
	metadb_handle_ptr f_handle;

	// ONLY HANDLE PLAYABLE FILES OR URLS, LINKING TO ANOTHER PLAYLIST IS NOT SUPPORTED
	const pfc::string8 out_str = uriToPath( track_location->GetText() , p_path , xml_base->getXmlBase() );
	if( !out_str.is_empty() )
	{
		p_callback->on_progress( out_str );
		p_callback->handle_create( f_handle , make_playable_location( out_str , 0 ) );
	}

	// 4.1.1.2.14.1.1.1.3 title
	addInfoHelper( x_track , &f_info , "title" , "TITLE" );

	// 4.1.1.2.14.1.1.1.4 creator
	addInfoHelper( x_track , &f_info , "creator" , "ARTIST" );

	// 4.1.1.2.14.1.1.1.5 annotation
	addInfoHelper( x_track , &f_info , "annotation" , "COMMENT" );

	// 4.1.1.2.14.1.1.1.5 album
	addInfoHelper( x_track , &f_info , "album" , "ALBUM" );

	// 4.1.1.2.14.1.1.1.9 trackNum
	addInfoHelper( x_track , &f_info , "trackNum" , "TRACKNUMBER" );

	// insert into playlist
	p_callback->on_entry_info( f_handle , playlist_loader_callback::entry_user_requested , filestats_invalid , f_info , false );

	return;
}

void open_helper_no_location( playlist_loader_callback::ptr p_callback , const tinyxml2::XMLElement *x_track , const dbList *in_list , lruCacheImpl *lru_cache )
{
	dbList list;

	// 4.1.1.2.14.1.1.1.5 album
	filterFieldHelper( x_track , in_list , "album" , "ALBUM" , &list , lru_cache );

	// 4.1.1.2.14.1.1.1.3 title
	filterFieldHelper( x_track , &list , "title" , "TITLE" , &list );

	// 4.1.1.2.14.1.1.1.4 creator
	filterFieldHelper( x_track , &list , "creator" , "ARTIST" , &list );

	// 4.1.1.2.14.1.1.1.9 trackNum
	filterFieldHelper( x_track , &list , "trackNum" , "TRACKNUMBER" , &list );

	// add result
	for( t_size i = 0 , max = list.get_count() ; i < max ; ++i )
	{
		p_callback->on_entry( list.get_item_ref( i ) , playlist_loader_callback::entry_from_playlist , filestats_invalid , false );
		if( true )
			break;
	}

	return;
}

void write_helper( const char *p_path , const service_ptr_t<file> &p_file , metadb_handle_list_cref p_data , abort_callback &p_abort , const bool w_location )
{
	// new xml document
	tinyxml2::XMLDocument x;
	//x.SetBOM( true );

	// xml declaration
	auto x_decl = x.NewDeclaration( NULL );
	x.InsertEndChild( x_decl );

	// 4.1.1 playlist
	auto x_playlist = x.NewElement( "playlist" );
	x.InsertEndChild( x_playlist );
	x_playlist->SetAttribute( "version" , 1 );
	x_playlist->SetAttribute( "xmlns" , "http://xspf.org/ns/0/" );

	/*
	// 4.1.1.2.8 date,  XML schema dateTime
	const time_t now = time( NULL );
	char time_buf[24] = { 0 };
	struct tm tmp_tm = { 0 };
	gmtime_s( &tmp_tm , &now );
	strftime( time_buf , ( sizeof( time_buf ) - 1 ) , "%Y-%m-%dT%H:%M:%SZ" , &tmp_tm );

	auto x_date = x.NewElement( "date" );
	x_playlist->InsertEndChild( x_date );
	x_date->SetText( time_buf );
	*/

	// 4.1.1.2.14 trackList
	auto x_tracklist = x.NewElement( "trackList" );
	x_playlist->InsertEndChild( x_tracklist );

	// for each track
	for( t_size i = 0 , max = p_data.get_size(); i < max ; ++i )
	{
		if( p_abort.is_aborting() )
			return;

		// fetch track info
		const metadb_handle_ptr track_item = p_data.get_item( i );
		const auto track_info = track_item->get_async_info_ref();

		// 4.1.1.2.14.1.1 track
		auto x_track = x.NewElement( "track" );
		x_tracklist->InsertEndChild( x_track );

		// 4.1.1.2.14.1.1.1.1 location
		if( w_location )
		{
			const char *item_path = track_item->get_path();
			const pfc::string8 track_path = pathToUri( item_path , p_path );
			if( !track_path.is_empty() )
			{
				auto track_location = x.NewElement( "location" );
				x_track->InsertEndChild( track_location );
				track_location->SetText( track_path );
			}
		}

		// 4.1.1.2.14.1.1.1.3 title
		if( track_info->info().meta_exists( "TITLE" ) )
		{
			const char *str = track_info->info().meta_get( "TITLE" , 0 );
			auto track_title = x.NewElement( "title" );
			x_track->InsertEndChild( track_title );
			track_title->SetText( str );
		}

		// 4.1.1.2.14.1.1.1.4 creator
		if( track_info->info().meta_exists( "ARTIST" ) )
		{
			const char *str = track_info->info().meta_get( "ARTIST" , 0 );
			auto track_creator = x.NewElement( "creator" );
			x_track->InsertEndChild( track_creator );
			track_creator->SetText( str );
		}

		// 4.1.1.2.14.1.1.1.5 annotation
		if( track_info->info().meta_exists( "COMMENT" ) )
		{
			const char *str = track_info->info().meta_get( "COMMENT" , 0 );
			auto track_annotation = x.NewElement( "annotation" );
			x_track->InsertEndChild( track_annotation );
			track_annotation->SetText( str );
		}

		// 4.1.1.2.14.1.1.1.8 album
		if( track_info->info().meta_exists( "ALBUM" ) )
		{
			const char *str = track_info->info().meta_get( "ALBUM" , 0 );
			auto track_album = x.NewElement( "album" );
			x_track->InsertEndChild( track_album );
			track_album->SetText( str );
		}

		// 4.1.1.2.14.1.1.1.9 trackNum
		if( track_info->info().meta_exists( "TRACKNUMBER" ) )
		{
			const char *str = track_info->info().meta_get( "TRACKNUMBER" , 0 );
			const long int num = strtol( str , NULL , 10 );
			if( num > 0 )
			{
				auto track_tracknum = x.NewElement( "trackNum" );
				x_track->InsertEndChild( track_tracknum );
				track_tracknum->SetText( num );
			}
		}

		// 4.1.1.2.14.1.1.1.10 duration, in MILLISECONDS!
		const double track_len = track_item->get_length() * 1000;
		if( track_len > 0 )
		{
			auto track_duration = x.NewElement( "duration" );
			x_track->InsertEndChild( track_duration );
			track_duration->SetText( ( t_size ) track_len );
		}
	}

	// output
	tinyxml2::XMLPrinter x_printer;
	x.Print( &x_printer );
	try
	{
		p_file->write_string_raw( x_printer.CStr() , p_abort );
	}
	catch( ... )
	{
		//try {filesystem::g_remove(p_path,p_abort);} catch(...) {}
		console::printf( CONSOLE_HEADER"write_string_raw exception" );
		throw;
	}

	return;
}


void addInfoHelper( const tinyxml2::XMLElement *x_parent , file_info_impl *f , const char* x_name , const char *db_name )
{
	const auto *info = x_parent->FirstChildElement( x_name );
	if( ( info != nullptr ) && ( info->GetText() != nullptr ) )
	{
		f->meta_add( db_name , info->GetText() );
	}

	return;
}

void filterFieldHelper( const tinyxml2::XMLElement *x_parent , const dbList *in_list , const char *x_name , const char *db_name , dbList *out , lruCacheImpl *lru_cache )
{
	// prepare
	const auto *x = x_parent->FirstChildElement( x_name );
	if( x == nullptr )
		return;
	const char *x_field = x->GetText();
	if( x_field == nullptr )
		return;

	// use cache
	const dbList *list = in_list;
	if( lru_cache != nullptr )
	{
		const dbList *t = lru_cache->get( x_field );
		if( t != nullptr )
		{
			list = t;
		}
	}

	// scan through list
	dbList tmp_list;
	for( t_size i = 0 , max = list->get_count(); i < max ; ++i )
	{
		// get item from db
		const auto item = list->get_item_ref( i );
		const auto info = item->get_async_info_ref();

		const char *str = info->info().meta_get( db_name , 0 );
		if( str == nullptr )
			continue;

		// try partial match
		const bool match = (strstr( str , x_field ) != nullptr) ? true : false;
		if( match )
		{
			tmp_list += item;
		}
	}

	// use cache
	if( lru_cache != nullptr )
	{
		lru_cache->set( x_field , &tmp_list );
	}

	out->move_from( tmp_list );
	return;
}


pfc::string8 pathToUri( const char *in_path , const char *ref_path )
{
	pfc::string8 out;

	pfc::string8 path_str = in_path;
	if( !filesystem::g_is_remote_safe( in_path ) )
	{
		// local path
		// try extract relative path
		pfc::string8 tmp_str;
		if( filesystem::g_relative_path_create( in_path , ref_path , tmp_str ) )
		{
			// have relative path
			path_str = tmp_str;
		}
		path_str.replace_string( "file://" , "file:///" );

		// when loaded a track with <location> while it's not in metadb and fb2k didn't reads its meta yet. the generated xspf playlist will not have "file://" scheme
		if( !path_str.has_prefix_i( "file:/" ) )
		{
			path_str.insert_chars(0,"file:///");
		}
	}

	// create URI
	path_str.replace_char( '\\' , '/' );  // note: linux can have '\' in file name
	out = urlEncodeUtf8( path_str );

	return out;
}

pfc::string8 uriToPath( const char *in_uri , const char *ref_path , const pfc::string8 base_str )
{
	pfc::string8 out;

	// add xml:base
	if( !base_str.is_empty() )
	{
		out += base_str;
	}

	// check "file:" scheme
	pfc::string8 in_str = urlDecodeUtf8( in_uri );
	const bool is_local = in_str.has_prefix( "file:" );
	if( is_local )
	{
		// prepare
		in_str.replace_string( "file:///" , "" );
		in_str.replace_string( "/" , "\\" );

		// check if relative path
		const bool is_relative_path = ( in_str.find_first( ':' ) < in_str.get_length() ) ? false : true ;
		if( is_relative_path )
		{
			// add parent path
			pfc::string8 par_path = ref_path;
			par_path.truncate_to_parent_path();
			par_path.fix_dir_separator();
			out += par_path;
		}
	}
	out += in_str;

	return out;
}


pfc::string8 urlEncodeUtf8( const char *in )
{
	// percent-encoding
	// `in` must be a utf-8 encoded & null terminated string

	std::string out;
	for( const char *i = in ; *i != '\0' ; )
	{
		if( ( *i >= 0 ) && ( isalnum( *i ) || ( *i == '-' ) || ( *i == '_' ) || ( *i == '.' ) || ( *i == '~' ) /*special case for URI, safe for windows*/ || ( *i == '/' ) || ( *i == ':' ) ) )
		{
			// RFC 3986 section 2.3, Unreserved Characters
			out += *i;
			++i;
		}
		else
		{
			int byte_len = 4;
			if( ( unsigned char ) *i < 192 )
				byte_len = 1;
			else if( ( unsigned char ) *i < 224 )
				byte_len = 2;
			else if( ( unsigned char ) *i < 240 )
				byte_len = 3;
			for( int j = 0; ( j < byte_len ) && ( *( i + j ) != '\0' ) ; ++j )
			{
				char tmp[4] = { 0 };
				sprintf_s( tmp , "%%%X" , ( unsigned char ) * ( i ) );
				out += tmp;
				++i;
			}
		}
	}

	return out.c_str();
}

pfc::string8 urlDecodeUtf8( const char *in )
{
	// `in` must be a percent-encoded utf-8 & null terminated string

	std::string out;
	for( const char *i = in ; *i != '\0' ; )
	{
		if( *i == '%' )
		{
			const char tmp[] = { *( i + 1 ) , *( i + 2 ) , '\0' };
			out += ( char ) strtol( tmp , NULL , 16 );
			i += 3;
		}
		else
		{
			out += *i;
			++i;
		}
	}

	return out.c_str();
}
