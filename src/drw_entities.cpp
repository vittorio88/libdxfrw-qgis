/******************************************************************************
**  libDXFrw - Library to read/write DXF files (ascii & binary)              **
**                                                                           **
**  Copyright (C) 2011-2015 José F. Soriano, rallazz@gmail.com               **
**                                                                           **
**  This library is free software, licensed under the terms of the GNU       **
**  General Public License as published by the Free Software Foundation,     **
**  either version 2 of the License, or (at your option) any later version.  **
**  You should have received a copy of the GNU General Public License        **
**  along with this program.  If not, see <http://www.gnu.org/licenses/>.    **
******************************************************************************/

#include <cstdlib>

#include "drw_entities.h"
#include "intern/dxfreader.h"
#include "intern/dwgbuffer.h"
#include "intern/drw_dbg.h"

#undef QGISDEBUG
#include "qgslogger.h"
#include <QStringList>

#define RESERVE( vector, size ) try { \
    vector.reserve(size); \
  } catch(const std::exception &e) { \
    QgsDebugMsg( QString( "allocation exception (size=%1; error=%2)" ).arg( size ).arg( e.what() ) ); \
    throw e; \
  }

//! Calculate arbitrary axis
/*!
*   Calculate arbitrary axis for apply extrusions
*  @author Rallaz
*/
void DRW_Entity::calculateAxis( DRW_Coord extPoint )
{
  //Follow the arbitrary DXF definitions for extrusion axes.
  if ( std::fabs( extPoint.x ) < 0.015625 && std::fabs( extPoint.y ) < 0.015625 )
  {
    //If we get here, implement Ax = Wy x N where Wy is [0,1,0] per the DXF spec.
    //The cross product works out to Wy.y*N.z-Wy.z*N.y, Wy.z*N.x-Wy.x*N.z, Wy.x*N.y-Wy.y*N.x
    //Factoring in the fixed values for Wy gives N.z,0,-N.x
    extAxisX.x = extPoint.z;
    extAxisX.y = 0;
    extAxisX.z = -extPoint.x;
  }
  else
  {
    //Otherwise, implement Ax = Wz x N where Wz is [0,0,1] per the DXF spec.
    //The cross product works out to Wz.y*N.z-Wz.z*N.y, Wz.z*N.x-Wz.x*N.z, Wz.x*N.y-Wz.y*N.x
    //Factoring in the fixed values for Wz gives -N.y,N.x,0.
    extAxisX.x = -extPoint.y;
    extAxisX.y = extPoint.x;
    extAxisX.z = 0;
  }

  extAxisX.unitize();

  //Ay = N x Ax
  extAxisY.x = ( extPoint.y * extAxisX.z ) - ( extAxisX.y * extPoint.z );
  extAxisY.y = ( extPoint.z * extAxisX.x ) - ( extAxisX.z * extPoint.x );
  extAxisY.z = ( extPoint.x * extAxisX.y ) - ( extAxisX.x * extPoint.y );

  extAxisY.unitize();
}

//! Extrude a point using arbitrary axis
/*!
*   apply extrusion in a point using arbitrary axis (previously calculated)
*  @author Rallaz
*/
void DRW_Entity::extrudePoint( DRW_Coord extPoint, DRW_Coord *point )
{
  double px, py, pz;
  px = ( extAxisX.x * point->x ) + ( extAxisY.x * point->y ) + ( extPoint.x * point->z );
  py = ( extAxisX.y * point->x ) + ( extAxisY.y * point->y ) + ( extPoint.y * point->z );
  pz = ( extAxisX.z * point->x ) + ( extAxisY.z * point->y ) + ( extPoint.z * point->z );

  point->x = px;
  point->y = py;
  point->z = pz;
}

bool DRW_Entity::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 5:
      handle = reader->getHandleString();
      break;
    case 330:
      parentHandle = reader->getHandleString();
      break;
    case 8:
      layer = reader->getUtf8String();
      break;
    case 6:
      lineType = reader->getUtf8String();
      break;
    case 62:
      color = reader->getInt32();
      break;
    case 370:
      lWeight = DRW_LW_Conv::dxfInt2lineWidth( reader->getInt32() );
      break;
    case 48:
      ltypeScale = reader->getDouble();
      break;
    case 60:
      visible = reader->getBool();
      break;
    case 420:
      color24 = reader->getInt32();
      break;
    case 430:
      colorName = reader->getString();
      break;
    case 440:
      transparency = reader->getInt32();
      break;
    case 67:
      space = static_cast<DRW::Space>( reader->getInt32() );
      break;
    case 102:
      parseDxfGroups( code, reader );
      break;
    case 1000:
    case 1001:
    case 1002:
    case 1003:
    case 1004:
    case 1005:
      extData.push_back( new DRW_Variant( code, reader->getString() ) );
      break;
    case 1010:
    case 1011:
    case 1012:
    case 1013:
      curr = new DRW_Variant( code, DRW_Coord( reader->getDouble(), 0.0, 0.0 ) );
      extData.push_back( curr );
      break;
    case 1020:
    case 1021:
    case 1022:
    case 1023:
      if ( curr )
        curr->setCoordY( reader->getDouble() );
      break;
    case 1030:
    case 1031:
    case 1032:
    case 1033:
      if ( curr )
        curr->setCoordZ( reader->getDouble() );
      curr = nullptr;
      break;
    case 1040:
    case 1041:
    case 1042:
      extData.push_back( new DRW_Variant( code, reader->getDouble() ) );
      break;
    case 1070:
    case 1071:
      extData.push_back( new DRW_Variant( code, reader->getInt32() ) );
      break;
    default:
      break;
  }
  return true;
}

//parses dxf 102 groups to read entity
bool DRW_Entity::parseDxfGroups( int code, dxfReader *reader )
{
  std::list<DRW_Variant> ls;
  DRW_Variant curr;
  int nc;
  std::string appName = reader->getString();
  if ( !appName.empty() && appName.at( 0 ) == '{' )
  {
    curr.addString( code, appName.substr( 1, ( int ) appName.size() - 1 ) );
    ls.push_back( curr );
    while ( code != 102 && appName.at( 0 ) == '}' )
    {
      reader->readRec( &nc );//RLZ curr.code = code or nc?
//            curr.code = code;
      //RLZ code == 330 || code == 360 OR nc == 330 || nc == 360 ?
      if ( code == 330 || code == 360 )
        curr.addInt( code, reader->getHandleString() );//RLZ code or nc
      else
      {
        switch ( reader->type )
        {
          case dxfReader::STRING:
            curr.addString( code, reader->getString() );//RLZ code or nc
            break;
          case dxfReader::INT32:
          case dxfReader::INT64:
            curr.addInt( code, reader->getInt32() );//RLZ code or nc
            break;
          case dxfReader::DOUBLE:
            curr.addDouble( code, reader->getDouble() );//RLZ code or nc
            break;
          case dxfReader::BOOL:
            curr.addInt( code, reader->getInt32() );//RLZ code or nc
            break;
          default:
            break;
        }
      }
      ls.push_back( curr );
    }
  }

  appData.push_back( ls );
  return true;
}

bool DRW_Entity::parseDwg( DRW::Version version, dwgBuffer *buf, dwgBuffer *strBuf, duint32 bs )
{
  objSize = 0;
  QgsDebugMsg( "***************************** parsing entity *********************************************" );
  oType = buf->getObjType( version );
  QgsDebugMsg( QString( "Object type: %1, 0x%2" ).arg( oType ).arg( oType, 0, 16 ) );

  if ( version > DRW::AC1014 && version < DRW::AC1024 )  //2000 & 2004
  {
    objSize = buf->getRawLong32();  //RL 32bits object size in bits
    QgsDebugMsg( QString( " Object size: %1" ).arg( objSize ) );
  }
  if ( version > DRW::AC1021 )  //2010+
  {
    duint32 ms = buf->size();
    objSize = ms * 8 - bs;
    QgsDebugMsg( QString( " Object size: %1" ).arg( objSize ) );
  }

  if ( strBuf && version > DRW::AC1018 )  //2007+
  {
    strBuf->moveBitPos( objSize - 1 );
    QgsDebugMsgLevel( QString( "strBuf strbit pos 2007:%1 strBuf bpos 2007:%2" )
                      .arg( strBuf->getPosition() ).arg( strBuf->getBitPos() ), 4
                    );
    if ( strBuf->getBit() == 1 )
    {
      QgsDebugMsgLevel( "string bit is 1", 4 );
      strBuf->moveBitPos( -17 );
      duint16 strDataSize = strBuf->getRawShort16();
      QgsDebugMsgLevel( QString( "strDataSize: %1" ).arg( strDataSize ), 4 );
      if ( ( strDataSize & 0x8000 ) == 0x8000 )
      {
        QgsDebugMsgLevel( "string 0x8000 bit is set", 4 );
        strBuf->moveBitPos( -33 );//RLZ pending to verify
        duint16 hiSize = strBuf->getRawShort16();
        strDataSize = ( ( strDataSize & 0x7fff ) | ( hiSize << 15 ) );
      }
      strBuf->moveBitPos( -strDataSize - 16 ); //-14

      QgsDebugMsgLevel( QString( "strBuf start strDataSize pos 2007:%1 strBuf bpos 2007:%2" )
                        .arg( strBuf->getPosition() ).arg( strBuf->getBitPos() ), 4
                      );
    }
    else
    {
      QgsDebugMsg( "string bit is 0" );
    }

    QgsDebugMsgLevel( QString( "strBuf start pos 2007:%1 strBuf bpos 2007:%2" )
                      .arg( strBuf->getPosition() ).arg( strBuf->getBitPos() ), 4
                    );
  }

  dwgHandle ho = buf->getHandle();
  handle = ho.ref;
  dint16 extDataSize = buf->getBitShort(); //BS
  QgsDebugMsg( QString( "Entity Handle: %1.%2 0x%3; ext data size:%4" ).arg( ho.code ).arg( ho.size ).arg( ho.ref, 0, 16 ).arg( extDataSize ) );
  while ( extDataSize > 0 && buf->isGood() )
  {
    /* RLZ: TODO */
    dwgHandle ah = buf->getHandle();
    QgsDebugMsg( QString( " App Handle: %1.%2 0x%3" ).arg( ah.code ).arg( ah.size ).arg( ah.ref, 0, 16 ) );
    duint8 *tmpExtData = new duint8[extDataSize];
    buf->getBytes( tmpExtData, extDataSize );
    dwgBuffer tmpExtDataBuf( tmpExtData, extDataSize, buf->decoder );

    duint8 dxfCode = tmpExtDataBuf.getRawChar8();
    QgsDebugMsg( QString( " dxfCode: %1" ).arg( dxfCode ) );

    switch ( dxfCode )
    {
      case 0:
      {
        duint8 strLength = tmpExtDataBuf.getRawChar8();
        duint16 cp = tmpExtDataBuf.getBERawShort16();
        Q_UNUSED( cp );

        QStringList l;
        for ( int i = 0; i < strLength + 1; i++ ) //string length + null terminating char
        {
          duint8 dxfChar = tmpExtDataBuf.getRawChar8();
          l << QStringLiteral( "0x%1" ).arg( dxfChar, 0, 16 );
        }

        QgsDebugMsg( QString( "strLength:%1; str codepage:%2; %3" ).arg( strLength ).arg( cp ).arg( l.join( " " ) ) );
        Q_UNUSED( l );
        break;
      }
      default:
        /* RLZ: TODO */
        break;
    }
    delete[]tmpExtData;
    extDataSize = buf->getBitShort(); //BS
    QgsDebugMsg( QString( " ext data size: %1" ).arg( extDataSize ) );
  } //end parsing extData (EED)
  duint8 graphFlag = buf->getBit(); //B
  QgsDebugMsg( QString( "graphFlag:%1" ).arg( graphFlag ) );
  if ( graphFlag )
  {
    duint32 graphDataSize = buf->getRawLong32();  //RL 32bits
    QgsDebugMsg( QString( "graphData in bytes: %1" ).arg( graphDataSize ) );
// RLZ: TODO
    //skip graphData bytes
    duint8 *tmpGraphData = new duint8[graphDataSize];
    buf->getBytes( tmpGraphData, graphDataSize );
    dwgBuffer tmpGraphDataBuf( tmpGraphData, graphDataSize, buf->decoder );
    QgsDebugMsg( QString( "graph data remaining bytes:%1" ).arg( tmpGraphDataBuf.numRemainingBytes() ) );
    delete[]tmpGraphData;
  }
  if ( version < DRW::AC1015 )  //14-
  {
    objSize = buf->getRawLong32();  //RL 32bits object size in bits
    QgsDebugMsg( QString( " Object size in bits: %1" ).arg( objSize ) );
  }

  duint8 entmode = buf->get2Bits(); //BB
  if ( entmode == 0 )
    ownerHandle = true;
//        entmode = 2;
  else if ( entmode == 2 )
    entmode = 0;

  space = ( DRW::Space )entmode; //RLZ verify cast values
  numReactors = buf->getBitShort(); //BS
  QgsDebugMsg( QString( "entmode:%1, numReactors: %2" ).arg( entmode ).arg( numReactors ) );

  if ( version < DRW::AC1015 )  //14-
  {
    if ( buf->getBit() )  //is bylayer line type
    {
      lineType = "BYLAYER";
      ltFlags = 0;
    }
    else
    {
      lineType = "";
      ltFlags = 3;
    }
    QgsDebugMsg( QString( " lineType:%1 ltFlags:%2" ).arg( lineType.c_str() ).arg( ltFlags ) );
  }
  if ( version > DRW::AC1015 )  //2004+
  {
    xDictFlag = buf->getBit();
    QgsDebugMsg( QString( " xDictFlag: %1" ).arg( xDictFlag ) );
  }

  if ( version > DRW::AC1024 || version < DRW::AC1018 )
  {
    haveNextLinks = buf->getBit(); //aka nolinks //B
    QgsDebugMsg( QString( "haveNextLinks (0 yes, 1 prev next): %1" ).arg( haveNextLinks ) );
  }
  else
  {
    haveNextLinks = 1; //aka nolinks //B
    QgsDebugMsg( QString( "haveNextLinks (forced): %1" ).arg( haveNextLinks ) );
  }
//ENC color
  color = buf->getEnColor( version, color24, transparency ); //BS or CMC //OK for R14 or negate
  ltypeScale = buf->getBitDouble(); //BD
  QgsDebugMsg( QString( " entity color:%1 ltScale:%2" ).arg( color ).arg( ltypeScale ) );

  if ( version > DRW::AC1014 )  //2000+
  {
    UTF8STRING plotStyleName;
    for ( duint8 i = 0; i < 2; ++i )   //two flags in one
    {
      plotFlags = buf->get2Bits(); //BB
      if ( plotFlags == 1 )
        plotStyleName = "byblock";
      else if ( plotFlags == 2 )
        plotStyleName = "continuous";
      else if ( plotFlags == 0 )
        plotStyleName = "bylayer";
      else //handle at end
        plotStyleName = "";
      if ( i == 0 )
      {
        ltFlags = plotFlags;
        lineType = plotStyleName; //RLZ: howto solve? if needed plotStyleName;
        QgsDebugMsg( QString( "ltFlags:%1 lineType:%2" ).arg( lineType.c_str() ).arg( ltFlags ) );
      }
      else
      {
        QgsDebugMsg( QString( "plotFlags:%1" ).arg( plotFlags ) );
      }
    }
  }
  if ( version > DRW::AC1018 )  //2007+
  {
    materialFlag = buf->get2Bits(); //BB
    shadowFlag = buf->getRawChar8(); //RC
    QgsDebugMsg( QString( " materialFlag:%1 shadowFlag:%2" ).arg( materialFlag ).arg( shadowFlag ) );
  }
  if ( version > DRW::AC1021 )  //2010+
  {
    duint8 visualFlags = buf->get2Bits(); //full & face visual style
    duint8 unk = buf->getBit(); //edge visual style
    QgsDebugMsg( QString( " shadowFlag 2:%1 unknown bit:%2" ).arg( visualFlags ).arg( unk ) );
    Q_UNUSED( visualFlags );
    Q_UNUSED( unk );
  }
  dint16 invisibleFlag = buf->getBitShort(); //BS
  QgsDebugMsg( QString( " invisibleFlag:%1" ).arg( invisibleFlag ) );
  Q_UNUSED( invisibleFlag );
  if ( version > DRW::AC1014 )  //2000+
  {
    lWeight = DRW_LW_Conv::dwgInt2lineWidth( buf->getRawChar8() ); //RC
    QgsDebugMsg( QString( " lwFlag (lWeight):%1" ).arg( lWeight ) );
    Q_UNUSED( lWeight );
  }
#if 0
  //Only in blocks ????????
  if ( version > DRW::AC1018 )  //2007+
  {
    duint8 unk = buf->getBit();
    QgsDebugMsg( QString( "unknown bit: %1" ).arg( unk ) );
  }
#endif
  return buf->isGood();
}

bool DRW_Entity::parseDwgEntHandle( DRW::Version version, dwgBuffer *buf )
{
  if ( version > DRW::AC1018 )  //2007+ skip string area
  {
    buf->setPosition( objSize >> 3 );
    buf->setBitPos( objSize & 7 );
  }

  if ( ownerHandle ) //entity are in block or in a polyline
  {
    dwgHandle ownerH = buf->getOffsetHandle( handle );
    QgsDebugMsg( QString( "owner (parent) Handle:%1.%2 0x%3 Remaining bytes:%4" )
                 .arg( ownerH.code ).arg( ownerH.size ).arg( ownerH.ref, 0, 16 )
                 .arg( buf->numRemainingBytes() )
               );
    parentHandle = ownerH.ref;
  }
  else
  {
    QgsDebugMsg( "NO Block (parent) Handle" );
  }

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  for ( int i = 0; i < numReactors; ++i )
  {
    dwgHandle reactorsH = buf->getHandle();
    QgsDebugMsg( QString( "reactorsH control Handle:%1.%2 0x%3" )
                 .arg( reactorsH.code ).arg( reactorsH.size ).arg( reactorsH.ref, 0, 16 )
               );
  }
  if ( xDictFlag != 1 ) //linetype in 2004 seems not have XDicObjH or NULL handle
  {
    dwgHandle XDicObjH = buf->getHandle();
    QgsDebugMsg( QString( "XDicObj control Handle:%1.%2 0x%3" )
                 .arg( XDicObjH.code ).arg( XDicObjH.size ).arg( XDicObjH.ref, 0, 16 )
               );
  }
  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( version < DRW::AC1015 )  //R14-
  {
    //layer handle
    layerH = buf->getOffsetHandle( handle );
    QgsDebugMsg( QString( " layer Handle:%1.%2 0x%3, remaining bytes %4" )
                 .arg( layerH.code ).arg( layerH.size ).arg( layerH.ref, 0, 16 )
                 .arg( buf->numRemainingBytes() )
               );

    //lineType handle
    if ( ltFlags == 3 )
    {
      lTypeH = buf->getOffsetHandle( handle );
      QgsDebugMsg( QString( " linetype Handle:%1.%2 0x%3, remaining bytes %4" )
                   .arg( lTypeH.code ).arg( lTypeH.size ).arg( lTypeH.ref, 0, 16 )
                   .arg( buf->numRemainingBytes() ) );
    }
  }
  if ( version < DRW::AC1018 )  //2000+
  {
    if ( haveNextLinks == 0 )
    {
      dwgHandle nextLinkH = buf->getOffsetHandle( handle );
      QgsDebugMsg( QString( " prev nextLinkers Handle:%1.%2 0x%3, remaining bytes %4" )
                   .arg( nextLinkH.code ).arg( nextLinkH.size ).arg( nextLinkH.ref, 0, 16 )
                   .arg( buf->numRemainingBytes() ) );
      prevEntLink = nextLinkH.ref;
      nextLinkH = buf->getOffsetHandle( handle );
      QgsDebugMsg( QString( " next nextLinkers Handle:%1.%2 0x%3, remaining bytes %4" )
                   .arg( nextLinkH.code ).arg( nextLinkH.size ).arg( nextLinkH.ref, 0, 16 )
                   .arg( buf->numRemainingBytes() ) );
      nextEntLink = nextLinkH.ref;
    }
    else
    {
      nextEntLink = handle + 1;
      prevEntLink = handle - 1;
    }
  }
  if ( version > DRW::AC1015 )  //2004+
  {
    //Parses Bookcolor handle
  }
  if ( version > DRW::AC1014 )  //2000+
  {
    //layer handle
    layerH = buf->getOffsetHandle( handle );
    QgsDebugMsg( QString( " layer Handle:%1.%2 0x%3, remaining bytes %4" )
                 .arg( layerH.code ).arg( layerH.size ).arg( layerH.ref, 0, 16 )
                 .arg( buf->numRemainingBytes() ) );
    //lineType handle
    if ( ltFlags == 3 )
    {
      lTypeH = buf->getOffsetHandle( handle );
      QgsDebugMsg( QString( " linetype Handle:%1.%2 0x%3, remaining bytes %4" )
                   .arg( lTypeH.code ).arg( lTypeH.size ).arg( lTypeH.ref, 0, 16 )
                   .arg( buf->numRemainingBytes() ) );
    }
  }
  if ( version > DRW::AC1014 )  //2000+
  {
    if ( version > DRW::AC1018 )  //2007+
    {
      if ( materialFlag == 3 )
      {
        dwgHandle materialH = buf->getOffsetHandle( handle );
        QgsDebugMsg( QString( " material handle:%1.%2 0x%3, remaining bytes %4" )
                     .arg( materialH.code ).arg( materialH.size ).arg( materialH.ref, 0, 16 )
                     .arg( buf->numRemainingBytes() ) );
      }
      if ( shadowFlag == 3 )
      {
        dwgHandle shadowH = buf->getOffsetHandle( handle );
        QgsDebugMsg( QString( " shadow handle:%1.%2 0x%3, remaining bytes %4" )
                     .arg( shadowH.code ).arg( shadowH.size ).arg( shadowH.ref, 0, 16 )
                     .arg( buf->numRemainingBytes() ) );
      }
    }
    if ( plotFlags == 3 )
    {
      dwgHandle plotStyleH = buf->getOffsetHandle( handle );
      QgsDebugMsg( QString( " plot style handle:%1.%2 0x%3, remaining bytes %4" )
                   .arg( plotStyleH.code ).arg( plotStyleH.size ).arg( plotStyleH.ref, 0, 16 )
                   .arg( buf->numRemainingBytes() ) );
    }
  }
  QgsDebugMsg( QString( "remaining bytes:%1" ).arg( buf->numRemainingBytes() ) );

  return buf->isGood();
}

void DRW_Point::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 10:
      basePoint.x = reader->getDouble();
      break;
    case 20:
      basePoint.y = reader->getDouble();
      break;
    case 30:
      basePoint.z = reader->getDouble();
      break;
    case 39:
      thickness = reader->getDouble();
      break;
    case 210:
      haveExtrusion = true;
      extPoint.x = reader->getDouble();
      break;
    case 220:
      extPoint.y = reader->getDouble();
      break;
    case 230:
      extPoint.z = reader->getDouble();
      break;
    default:
      DRW_Entity::parseCode( code, reader );
      break;
  }
}

bool DRW_Point::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing point *********************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();
  thickness = buf->getThickness( version > DRW::AC1014 );//BD
  extPoint = buf->getExtrusion( version > DRW::AC1014 );
  double x_axis = buf->getBitDouble();//BD

  QgsDebugMsg( QString( "point:%1 thickness:%2, extrusion:%3, x_axis:%4" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( thickness )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
               .arg( x_axis )
             );
  Q_UNUSED( x_axis );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  //    RS crc;   //RS */

  return buf->isGood();
}

void DRW_Line::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 11:
      secPoint.x = reader->getDouble();
      break;
    case 21:
      secPoint.y = reader->getDouble();
      break;
    case 31:
      secPoint.z = reader->getDouble();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

bool DRW_Line::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing line *********************************************" );

  if ( version < DRW::AC1015 )  //14-
  {
    basePoint.x = buf->getBitDouble();
    basePoint.y = buf->getBitDouble();
    basePoint.z = buf->getBitDouble();
    secPoint.x = buf->getBitDouble();
    secPoint.y = buf->getBitDouble();
    secPoint.z = buf->getBitDouble();
  }
  if ( version > DRW::AC1014 )  //2000+
  {
    bool zIsZero = buf->getBit(); //B
    basePoint.x = buf->getRawDouble();//RD
    secPoint.x = buf->getDefaultDouble( basePoint.x );//DD
    basePoint.y = buf->getRawDouble();//RD
    secPoint.y = buf->getDefaultDouble( basePoint.y );//DD
    if ( !zIsZero )
    {
      basePoint.z = buf->getRawDouble();//RD
      secPoint.z = buf->getDefaultDouble( basePoint.z );//DD
    }
  }

  thickness = buf->getThickness( version > DRW::AC1014 );//BD
  extPoint = buf->getExtrusion( version > DRW::AC1014 );
  QgsDebugMsg( QString( "startpoint:%1 endpoint:%2 thickness:%3 extrusion:%4" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( thickness )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
             );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  // RS crc;   //RS */

  return buf->isGood();
}

bool DRW_Ray::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing ray/xline *********************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();
  secPoint.x = buf->getBitDouble();
  secPoint.y = buf->getBitDouble();
  secPoint.z = buf->getBitDouble();

  QgsDebugMsg( QString( "startpoint:%1 vector:%2" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
             );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Circle::applyExtrusion()
{
  if ( haveExtrusion )
  {
    //NOTE: Commenting these out causes the the arcs being tested to be located
    //on the other side of the y axis (all x dimensions are negated).
    calculateAxis( extPoint );
    extrudePoint( extPoint, &basePoint );
  }
}

void DRW_Circle::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 40:
      mRadius = reader->getDouble();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

bool DRW_Circle::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing circle *********************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();
  mRadius = buf->getBitDouble();
  thickness = buf->getThickness( version > DRW::AC1014 );
  extPoint = buf->getExtrusion( version > DRW::AC1014 );

  QgsDebugMsg( QString( "center:%1 radius:%2 thickness:%3 extrusion:%4" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( mRadius ).arg( thickness )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
             );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Arc::applyExtrusion()
{
  DRW_Circle::applyExtrusion();

  if ( haveExtrusion )
  {
    // If the extrusion vector has a z value less than 0, the angles for the arc
    // have to be mirrored since DXF files use the right hand rule.
    // Note that the following code only handles the special case where there is a 2D
    // drawing with the z axis heading into the paper (or rather screen). An arbitrary
    // extrusion axis (with x and y values greater than 1/64) may still have issues.
    if ( std::fabs( extPoint.x ) < 0.015625 && std::fabs( extPoint.y ) < 0.015625 && extPoint.z < 0.0 )
    {
      staangle = M_PI - staangle;
      endangle = M_PI - endangle;

      double temp = staangle;
      staangle = endangle;
      endangle = temp;
    }
  }
}

void DRW_Arc::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 50:
      staangle = reader->getDouble() / ARAD;
      break;
    case 51:
      endangle = reader->getDouble() / ARAD;
      break;
    default:
      DRW_Circle::parseCode( code, reader );
      break;
  }
}

bool DRW_Arc::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing circle arc *********************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();
  mRadius = buf->getBitDouble();
  thickness = buf->getThickness( version > DRW::AC1014 );
  extPoint = buf->getExtrusion( version > DRW::AC1014 );
  staangle = buf->getBitDouble();
  endangle = buf->getBitDouble();

  QgsDebugMsg( QString( "center:%1,%2,%3 radius:%4 thickness:%5 extrusion:%6,%7,%8 staangle:%9 endangle:%10" )
               .arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z )
               .arg( mRadius ).arg( thickness )
               .arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z )
               .arg( staangle ).arg( endangle )
             );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  return buf->isGood();
}

void DRW_Ellipse::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 40:
      ratio = reader->getDouble();
      break;
    case 41:
      staparam = reader->getDouble();
      break;
    case 42:
      endparam = reader->getDouble();
      break;
    default:
      DRW_Line::parseCode( code, reader );
      break;
  }
}

void DRW_Ellipse::applyExtrusion()
{
  if ( haveExtrusion )
  {
    calculateAxis( extPoint );
    extrudePoint( extPoint, &secPoint );
    double initialparam = staparam;
    if ( extPoint.z < 0. )
    {
      staparam = M_PIx2 - endparam;
      endparam = M_PIx2 - initialparam;
    }
  }
}

//if ratio > 1 minor axis are greather than major axis, correct it
void DRW_Ellipse::correctAxis()
{
  bool complete = false;
  if ( staparam == endparam )
  {
    staparam = 0.0;
    endparam = M_PIx2; //2*M_PI;
    complete = true;
  }
  if ( ratio > 1 )
  {
    if ( std::fabs( endparam - staparam - M_PIx2 ) < 1.0e-10 )
      complete = true;
    double incX = secPoint.x;
    secPoint.x = -( secPoint.y * ratio );
    secPoint.y = incX * ratio;
    ratio = 1 / ratio;
    if ( !complete )
    {
      if ( staparam < M_PI_2 )
        staparam += M_PI * 2;
      if ( endparam < M_PI_2 )
        endparam += M_PI * 2;
      endparam -= M_PI_2;
      staparam -= M_PI_2;
    }
  }
}

bool DRW_Ellipse::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing ellipse *********************************************" );

  basePoint = buf->get3BitDouble();
  secPoint = buf->get3BitDouble();
  extPoint = buf->get3BitDouble();
  ratio = buf->getBitDouble();//BD
  staparam = buf->getBitDouble();//BD
  endparam = buf->getBitDouble();//BD

  QgsDebugMsg( QString( "center:%1 axis:%2 extrusion:%3 ratio:%4 staparam:%5 endparam:%6" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
               .arg( ratio ).arg( staparam ).arg( endparam )
             );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  //    RS crc;   //RS */

  return buf->isGood();
}

//parts are the number of vertices to split the polyline, default 128
void DRW_Ellipse::toPolyline( DRW_Polyline *pol, int parts ) const
{
  double radMajor = std::sqrt( secPoint.x * secPoint.x + secPoint.y * secPoint.y );
  double radMinor = radMajor * ratio;
  //calculate sin & std::cos of included angle
  double incAngle = std::atan2( secPoint.y, secPoint.x );
  double cosRot = std::cos( incAngle );
  double sinRot = std::sin( incAngle );

  incAngle = M_PIx2 / parts;
  double curAngle = staparam;
  double endAngle = endparam;
  if ( endAngle <= curAngle )
    endAngle += M_PIx2;

  while ( curAngle < endAngle )
  {
    double cosCurr = std::cos( curAngle );
    double sinCurr = std::sin( curAngle );
    double x = basePoint.x + cosCurr * cosRot * radMajor - sinCurr * sinRot * radMinor;
    double y = basePoint.y + cosCurr * sinRot * radMajor + sinCurr * cosRot * radMinor;
    pol->addVertex( DRW_Vertex( x, y, 0.0, 0.0 ) );
    curAngle += incAngle;
  }

  if ( std::fabs( endAngle - staparam - M_PIx2 ) < 1.0e-10 )
  {
    pol->flags = 1;
  }

  pol->layer = layer;
  pol->lineType = lineType;
  pol->color = color;
  pol->lWeight = lWeight;
  pol->extPoint = extPoint;
}

void DRW_Trace::applyExtrusion()
{
  if ( haveExtrusion )
  {
    calculateAxis( extPoint );
    extrudePoint( extPoint, &basePoint );
    extrudePoint( extPoint, &secPoint );
    extrudePoint( extPoint, &thirdPoint );
    extrudePoint( extPoint, &fourthPoint );
  }
}

void DRW_Trace::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 12:
      thirdPoint.x = reader->getDouble();
      break;
    case 22:
      thirdPoint.y = reader->getDouble();
      break;
    case 32:
      thirdPoint.z = reader->getDouble();
      break;
    case 13:
      fourthPoint.x = reader->getDouble();
      break;
    case 23:
      fourthPoint.y = reader->getDouble();
      break;
    case 33:
      fourthPoint.z = reader->getDouble();
      break;
    default:
      DRW_Line::parseCode( code, reader );
      break;
  }
}

bool DRW_Trace::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing Trace *********************************************" );

  thickness = buf->getThickness( version > DRW::AC1014 );
  basePoint.z = buf->getBitDouble();
  basePoint.x = buf->getRawDouble();
  basePoint.y = buf->getRawDouble();
  secPoint.x = buf->getRawDouble();
  secPoint.y = buf->getRawDouble();
  secPoint.z = basePoint.z;
  thirdPoint.x = buf->getRawDouble();
  thirdPoint.y = buf->getRawDouble();
  thirdPoint.z = basePoint.z;
  fourthPoint.x = buf->getRawDouble();
  fourthPoint.y = buf->getRawDouble();
  fourthPoint.z = basePoint.z;
  extPoint = buf->getExtrusion( version > DRW::AC1014 );

  QgsDebugMsg( QString( "base:%1 sec:%2 third:%3 fourth:%4 extrusion:%5 thickness:%6" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( thirdPoint.x ).arg( thirdPoint.y ).arg( thirdPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( fourthPoint.x ).arg( fourthPoint.y ).arg( fourthPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
               .arg( thickness )
             );

  /* Common Entity Handle Data */
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  /* CRC X --- */
  return buf->isGood();
}


void DRW_Solid::parseCode( int code, dxfReader *reader )
{
  DRW_Trace::parseCode( code, reader );
}

bool DRW_Solid::parseDwg( DRW::Version v, dwgBuffer *buf, duint32 bs )
{
  QgsDebugMsg( "***************************** parsing Solid *********************************************" );
  return DRW_Trace::parseDwg( v, buf, bs );
}

void DRW_3Dface::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 70:
      invisibleflag = reader->getInt32();
      Q_UNUSED( invisibleflag );
      break;
    default:
      DRW_Trace::parseCode( code, reader );
      break;
  }
}

bool DRW_3Dface::parseDwg( DRW::Version v, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( v, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing 3Dface *********************************************" );

  if ( v < DRW::AC1015 )  // R13 & R14
  {
    basePoint.x = buf->getBitDouble();
    basePoint.y = buf->getBitDouble();
    basePoint.z = buf->getBitDouble();
    secPoint.x = buf->getBitDouble();
    secPoint.y = buf->getBitDouble();
    secPoint.z = buf->getBitDouble();
    thirdPoint.x = buf->getBitDouble();
    thirdPoint.y = buf->getBitDouble();
    thirdPoint.z = buf->getBitDouble();
    fourthPoint.x = buf->getBitDouble();
    fourthPoint.y = buf->getBitDouble();
    fourthPoint.z = buf->getBitDouble();
    invisibleflag = buf->getBitShort();
  }
  else   // 2000+
  {
    bool has_no_flag = buf->getBit();
    bool z_is_zero = buf->getBit();
    basePoint.x = buf->getRawDouble();
    basePoint.y = buf->getRawDouble();
    basePoint.z = z_is_zero ? 0.0 : buf->getRawDouble();
    secPoint.x = buf->getDefaultDouble( basePoint.x );
    secPoint.y = buf->getDefaultDouble( basePoint.y );
    secPoint.z = buf->getDefaultDouble( basePoint.z );
    thirdPoint.x = buf->getDefaultDouble( secPoint.x );
    thirdPoint.y = buf->getDefaultDouble( secPoint.y );
    thirdPoint.z = buf->getDefaultDouble( secPoint.z );
    fourthPoint.x = buf->getDefaultDouble( thirdPoint.x );
    fourthPoint.y = buf->getDefaultDouble( thirdPoint.y );
    fourthPoint.z = buf->getDefaultDouble( thirdPoint.z );
    invisibleflag = has_no_flag ? ( int )NoEdge : buf->getBitShort();
  }
  drw_assert( invisibleflag >= NoEdge );
  drw_assert( invisibleflag <= AllEdges );  //#spellok

  QgsDebugMsg( QString( "base:%1 sec:%2 third:%3 fourth:%4 invisibleFlag:%5" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( thirdPoint.x ).arg( thirdPoint.y ).arg( thirdPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( fourthPoint.x ).arg( fourthPoint.y ).arg( fourthPoint.z ) )
               .arg( invisibleflag )
             );

  // Common Entity Handle Data
  ret = DRW_Entity::parseDwgEntHandle( v, buf );
  if ( !ret )
    return ret;
  return buf->isGood();
}

void DRW_Block::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 2:
      name = reader->getUtf8String();
      break;
    case 70:
      flags = reader->getInt32();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

bool DRW_Block::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  if ( !isEnd )
  {
    QgsDebugMsg( "***************************** parsing block *********************************************" );
    name = sBuf->getVariableText( version, false );
    QgsDebugMsg( QString( "Block name: %1" ).arg( name.c_str() ) );
  }
  else
  {
    QgsDebugMsg( "***************************** parsing end block *********************************************" );
  }
  if ( version > DRW::AC1018 )  //2007+
  {
    duint8 unk = buf->getBit();
    QgsDebugMsg( QString( "unknown bit: %1" ).arg( unk ) );
    Q_UNUSED( unk );
  }
//    X handleAssoc;   //X
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;
//    RS crc;   //RS */
  return buf->isGood();
}

void DRW_Insert::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 2:
      name = reader->getUtf8String();
      break;
    case 41:
      xscale = reader->getDouble();
      break;
    case 42:
      yscale = reader->getDouble();
      break;
    case 43:
      zscale = reader->getDouble();
      break;
    case 50:
      angle = reader->getDouble() / ARAD;
      break;
    case 70:
      colcount = reader->getInt32();
      break;
    case 71:
      rowcount = reader->getInt32();
      break;
    case 44:
      colspace = reader->getDouble();
      break;
    case 45:
      rowspace = reader->getDouble();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

bool DRW_Insert::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dint32 objCount = 0;
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "************************** parsing insert/minsert *****************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();

  QgsDebugMsg( QString( "insertion point:%1" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
             );

  if ( version < DRW::AC1015 )  //14-
  {
    xscale = buf->getBitDouble();
    yscale = buf->getBitDouble();
    zscale = buf->getBitDouble();
  }
  else
  {
    duint8 dataFlags = buf->get2Bits();
    if ( dataFlags == 3 )
    {
      //none default value 1,1,1
    }
    else if ( dataFlags == 1 )  //x default value 1, y & z can be x value
    {
      yscale = buf->getDefaultDouble( xscale );
      zscale = buf->getDefaultDouble( xscale );
    }
    else if ( dataFlags == 2 )
    {
      xscale = buf->getRawDouble();
      yscale = zscale = xscale;
    }
    else   //dataFlags == 0
    {
      xscale = buf->getRawDouble();
      yscale = buf->getDefaultDouble( xscale );
      zscale = buf->getDefaultDouble( xscale );
    }
  }

  angle = buf->getBitDouble();
  extPoint = buf->getExtrusion( false ); //3BD R14 style

  QgsDebugMsg( QString( "scale:%1, angle:%2 extrusion:%3" )
               .arg( QString( "%1,%2,%3" ).arg( xscale ).arg( yscale ).arg( zscale ) )
               .arg( angle )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
             );

  bool hasAttrib = buf->getBit();

  QgsDebugMsg( QString( "   has Attrib:%1" ).arg( hasAttrib ) );

  if ( hasAttrib && version > DRW::AC1015 )  //2004+
  {
    objCount = buf->getBitLong();
    Q_UNUSED( objCount );
    QgsDebugMsg( QString( "objCount:%1" ).arg( objCount ) );
  }
  if ( oType == 8 )  //entity are minsert
  {
    colcount = buf->getBitShort();
    rowcount = buf->getBitShort();
    colspace = buf->getBitDouble();
    rowspace = buf->getBitDouble();
  }

  QgsDebugMsg( QString( "   Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  blockRecH = buf->getHandle(); /* H 2 BLOCK HEADER (hard pointer) */

  QgsDebugMsg( QString( "   BLOCK HEADER Handle: %1.%2 0x%3, remaining bytes %4" )
               .arg( blockRecH.code ).arg( blockRecH.size ).arg( blockRecH.ref, 0, 16 )
               .arg( buf->numRemainingBytes() )
             );

  // attribs follows
  if ( hasAttrib )
  {
    if ( version < DRW::AC1018 )  //2000-
    {
      dwgHandle attH = buf->getHandle(); /* H 2 BLOCK HEADER (hard pointer) */

      QgsDebugMsg( QString( "   first attrib handle: %1.%2 0x%3" )
                   .arg( attH.code ).arg( attH.size ).arg( attH.ref, 0, 16 )
                 );

      attH = buf->getHandle(); /* H 2 BLOCK HEADER (hard pointer) */

      QgsDebugMsg( QString( "   second attrib handle: %1.%2 0x%3" )
                   .arg( attH.code ).arg( attH.size ).arg( attH.ref, 0, 16 )
                 );
    }
    else
    {
      for ( duint8 i = 0; i < objCount; ++i )
      {
        dwgHandle attH = buf->getHandle(); /* H 2 BLOCK HEADER (hard pointer) */
        QgsDebugMsg( QString( "   attrib handle #%1: %2.%3 0x%4" )
                     .arg( i ).arg( attH.code ).arg( attH.size ).arg( attH.ref, 0, 16 )
                   );
      }
    }
    seqendH = buf->getHandle(); /* H 2 BLOCK HEADER (hard pointer) */

    QgsDebugMsg( QString( "   seqend handle: %1.%2 0x%3" )
                 .arg( seqendH.code ).arg( seqendH.size ).arg( seqendH.ref, 0, 16 )
               );
  }

  QgsDebugMsg( QString( "   Remaining bytes:%1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  //    RS crc;   //RS */

  return buf->isGood();
}

void DRW_LWPolyline::applyExtrusion()
{
  if ( haveExtrusion )
  {
    calculateAxis( extPoint );
    for ( unsigned int i = 0; i < vertlist.size(); i++ )
    {
      DRW_Vertex2D *vert = vertlist.at( i );
      DRW_Coord v( vert->x, vert->y, elevation );
      extrudePoint( extPoint, &v );
      vert->x = v.x;
      vert->y = v.y;
    }
  }
}

void DRW_LWPolyline::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 10:
    {
      vertex = new DRW_Vertex2D();
      vertlist.push_back( vertex );
      vertex->x = reader->getDouble();
      break;
    }
    case 20:
      if ( vertex )
        vertex->y = reader->getDouble();
      break;
    case 40:
      if ( vertex )
        vertex->stawidth = reader->getDouble();
      break;
    case 41:
      if ( vertex )
        vertex->endwidth = reader->getDouble();
      break;
    case 42:
      if ( vertex )
        vertex->bulge = reader->getDouble();
      break;
    case 38:
      elevation = reader->getDouble();
      break;
    case 39:
      thickness = reader->getDouble();
      break;
    case 43:
      width = reader->getDouble();
      break;
    case 70:
      flags = reader->getInt32();
      break;
    case 90:
      vertexnum = reader->getInt32();
      RESERVE( vertlist, vertexnum );
      break;
    case 210:
      haveExtrusion = true;
      extPoint.x = reader->getDouble();
      break;
    case 220:
      extPoint.y = reader->getDouble();
      break;
    case 230:
      extPoint.z = reader->getDouble();
      break;
    default:
      DRW_Entity::parseCode( code, reader );
      break;
  }
}

bool DRW_LWPolyline::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing LWPolyline *******************************************" );

  flags = buf->getBitShort();
  QgsDebugMsg( QString( "flags value:0x%1" ).arg( flags, 0, 16 ) );

  if ( flags & 4 )
    width = buf->getBitDouble();
  if ( flags & 8 )
    elevation = buf->getBitDouble();
  if ( flags & 2 )
    thickness = buf->getBitDouble();
  if ( flags & 1 )
    extPoint = buf->getExtrusion( false );

  vertexnum = buf->getBitLong();
  RESERVE( vertlist, vertexnum );
  unsigned int bulgesnum = 0;
  if ( flags & 16 )
    bulgesnum = buf->getBitLong();
  int vertexIdCount = 0;
  if ( version > DRW::AC1021 )  //2010+
  {
    if ( flags & 1024 )
      vertexIdCount = buf->getBitLong();
  }

  unsigned int widthsnum = 0;
  if ( flags & 32 )
    widthsnum = buf->getBitLong();

  QgsDebugMsg( QString( "vertex num:%1, bulges num:%2, vertexIdCount:%3,  widths num:%4" )
               .arg( vertexnum ).arg( bulgesnum ).arg( vertexIdCount ).arg( widthsnum )
             );

  //clear all bit except 128 = plinegen and set 1 to open/close //RLZ:verify plinegen & open
  //dxf: plinegen 128 & open 1
  flags = ( flags & 512 ) ? ( flags | 1 ) : ( flags | 0 );
  flags &= 129;

  QgsDebugMsg( QString( "end flags value:0x%1" ).arg( flags, 0, 16 ) );

  if ( vertexnum > 0 ) //verify if is lwpol without vertex (empty)
  {
    // add vertexs
    vertex = new DRW_Vertex2D();
    vertex->x = buf->getRawDouble();
    vertex->y = buf->getRawDouble();
    vertlist.push_back( vertex );
    DRW_Vertex2D *pv = vertex;
    for ( std::vector<DRW_Vertex2D *>::size_type i = 1; i < vertexnum; i++ )
    {
      vertex = new DRW_Vertex2D();
      if ( version < DRW::AC1015 )  //14-
      {
        vertex->x = buf->getRawDouble();
        vertex->y = buf->getRawDouble();
      }
      else
      {
        vertex->x = buf->getDefaultDouble( pv->x );
        vertex->y = buf->getDefaultDouble( pv->y );
      }
      pv = vertex;
      vertlist.push_back( vertex );
    }
    //add bulges
    for ( unsigned int i = 0; i < bulgesnum; i++ )
    {
      double bulge = buf->getBitDouble();
      if ( vertlist.size() > i )
        vertlist.at( i )->bulge = bulge;
    }
    //add vertexId
    if ( version > DRW::AC1021 )  //2010+
    {
      for ( int i = 0; i < vertexIdCount; i++ )
      {
        dint32 vertexId = buf->getBitLong();
        //TODO implement vertexId, do not exist in dxf
        DRW_UNUSED( vertexId );
#if 0
        if ( vertlist.size() < i )
          vertlist.at( i )->vertexId = vertexId;
#endif
      }
    }
    //add widths
    for ( unsigned int i = 0; i < widthsnum; i++ )
    {
      double staW = buf->getBitDouble();
      double endW = buf->getBitDouble();
      if ( i < vertlist.size() )
      {
        vertlist.at( i )->stawidth = staW;
        vertlist.at( i )->endwidth = endW;
      }
    }
  }

  QgsDebugMsgLevel( "Vertex list: ", 5 );
  for ( std::vector<DRW_Vertex2D *>::iterator it = vertlist.begin() ; it != vertlist.end(); ++it )
  {
    DRW_Vertex2D *pv = *it;

    QgsDebugMsgLevel( QString( "x:%1 y:%2 bulge:%3 stawidth:%4 endwidth:%5" )
                      .arg( pv->x ).arg( pv->y )
                      .arg( pv->bulge )
                      .arg( pv->stawidth )
                      .arg( pv->endwidth ), 5
                    );
    Q_UNUSED( pv );
  }

  // Common Entity Handle Data
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  // CRC X

  return buf->isGood();
}


void DRW_Text::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 40:
      height = reader->getDouble();
      break;
    case 41:
      widthscale = reader->getDouble();
      break;
    case 50:
      angle = reader->getDouble() / ARAD;
      break;
    case 51:
      oblique = reader->getDouble();
      break;
    case 71:
      textgen = reader->getInt32();
      break;
    case 72:
      alignH = ( HAlign )reader->getInt32();
      break;
    case 73:
      alignV = ( VAlign )reader->getInt32();
      break;
    case 1:
      text = reader->getUtf8String();
      break;
    case 7:
      style = reader->getUtf8String();
      break;
    default:
      DRW_Line::parseCode( code, reader );
      break;
  }
}

bool DRW_Text::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing text *********************************************" );

  // DataFlags RC Used to determine presence of subsequent data, set to 0xFF for R14-
  duint8 data_flags = 0x00;
  if ( version > DRW::AC1014 )  //2000+
  {
    data_flags = buf->getRawChar8(); /* DataFlags RC Used to determine presence of subsequent data */

    QgsDebugMsg( QString( "data_flags:%1" ).arg( data_flags, 0, 16 ) );

    if ( !( data_flags & 0x01 ) ) /* Elevation RD --- present if !(DataFlags & 0x01) */
    {
      basePoint.z = buf->getRawDouble();
    }
  }
  else  //14-
  {
    basePoint.z = buf->getBitDouble(); /* Elevation BD --- */
  }
  basePoint.x = buf->getRawDouble(); /* Insertion pt 2RD 10 */
  basePoint.y = buf->getRawDouble();

  QgsDebugMsg( QString( "Insert point:%1" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
             );

  if ( version > DRW::AC1014 )  //2000+
  {
    if ( !( data_flags & 0x02 ) ) /* Alignment pt 2DD 11 present if !(DataFlags & 0x02), use 10 & 20 values for 2 default values.*/
    {
      secPoint.x = buf->getDefaultDouble( basePoint.x );
      secPoint.y = buf->getDefaultDouble( basePoint.y );
    }
    else
    {
      secPoint = basePoint;
    }
  }
  else  //14-
  {
    secPoint.x = buf->getRawDouble();  /* Alignment pt 2RD 11 */
    secPoint.y = buf->getRawDouble();
  }
  secPoint.z = basePoint.z;
  extPoint = buf->getExtrusion( version > DRW::AC1014 );
  thickness = buf->getThickness( version > DRW::AC1014 ); /* Thickness BD 39 */

  QgsDebugMsg( QString( "alignment:%1 extrusion:%2" )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
             );

  if ( version > DRW::AC1014 )  //2000+
  {
    if ( !( data_flags & 0x04 ) ) /* Oblique angle RD 51 present if !(DataFlags & 0x04) */
    {
      oblique = buf->getRawDouble();
    }
    if ( !( data_flags & 0x08 ) ) /* Rotation angle RD 50 present if !(DataFlags & 0x08) */
    {
      angle = buf->getRawDouble();
    }
    height = buf->getRawDouble(); /* Height RD 40 */
    if ( !( data_flags & 0x10 ) ) /* Width factor RD 41 present if !(DataFlags & 0x10) */
    {
      widthscale = buf->getRawDouble();
    }
  }
  else  //14-
  {
    oblique = buf->getBitDouble(); /* Oblique angle BD 51 */
    angle = buf->getBitDouble(); /* Rotation angle BD 50 */
    height = buf->getBitDouble(); /* Height BD 40 */
    widthscale = buf->getBitDouble(); /* Width factor BD 41 */
  }

  QgsDebugMsg( QString( "thickness:%1, Oblique angle:%2, Width:%3, rotation:%4, height:%5" )
               .arg( thickness ).arg( oblique ).arg( widthscale ).arg( angle ).arg( height )
             );

  text = sBuf->getVariableText( version, false ); /* Text value TV 1 */

  QgsDebugMsg( QString( "text string:%1" ).arg( text.c_str() ) );

  //textgen, alignH, alignV always present in R14-, data_flags set in initialization
  if ( !( data_flags & 0x20 ) ) /* Generation BS 71 present if !(DataFlags & 0x20) */
  {
    textgen = buf->getBitShort();
    QgsDebugMsg( QString( "textgen:%1" ).arg( textgen ) );
  }
  if ( !( data_flags & 0x40 ) ) /* Horiz align. BS 72 present if !(DataFlags & 0x40) */
  {
    alignH = ( HAlign )buf->getBitShort();
    QgsDebugMsg( QString( "alignH:%1" ).arg( alignH ) );
  }
  if ( !( data_flags & 0x80 ) ) /* Vert align. BS 73 present if !(DataFlags & 0x80) */
  {
    alignV = ( VAlign )buf->getBitShort();
    QgsDebugMsg( QString( "alignV:%1" ).arg( alignV ) );
  }

  /* Common Entity Handle Data */
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  styleH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "text style Handle:%1.%2 0x%3" )
               .arg( styleH.code ).arg( styleH.size ).arg( styleH.ref, 0, 16 )
             );

  /* CRC X --- */
  return buf->isGood();
}

void DRW_MText::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 1:
      text += reader->getString();
      text = reader->toUtf8String( text );
      break;
    case 11:
      haveXAxis = true;
      DRW_Text::parseCode( code, reader );
      break;
    case 3:
      text += reader->getString();
      break;
    case 44:
      interlin = reader->getDouble();
      break;
    default:
      DRW_Text::parseCode( code, reader );
      break;
  }
}

bool DRW_MText::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing mtext *********************************************" );

  basePoint = buf->get3BitDouble(); /* Insertion pt 3BD 10 - First picked point. */

  QgsDebugMsg( QString( "insertion:%1" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
             );

  extPoint = buf->get3BitDouble(); /* Extrusion 3BD 210 Undocumented; */
  secPoint = buf->get3BitDouble(); /* X-axis dir 3BD 11 */
  updateAngle();
  widthscale = buf->getBitDouble(); /* Rect width BD 41 */
  if ( version > DRW::AC1018 )  //2007+
  {
    /* Rect height BD 46 Reference rectangle height. */
    //! @todo
    buf->getBitDouble();
  }
  height = buf->getBitDouble();/* Text height BD 40 Undocumented */
  textgen = buf->getBitShort(); /* Attachment BS 71 Similar to justification; */
  /* Drawing dir BS 72 Left to right, etc.; see DXF doc */
  dint16 draw_dir = buf->getBitShort();
  DRW_UNUSED( draw_dir );
  /* Extents ht BD Undocumented and not present in DXF or entget */
  double ext_ht = buf->getBitDouble();
  DRW_UNUSED( ext_ht );
  /* Extents wid BD Undocumented and not present in DXF or entget The extents
  rectangle, when rotated the same as the text, fits the actual text image on
  the screen (although we've seen it include an extra row of text in height). */
  double ext_wid = buf->getBitDouble();
  DRW_UNUSED( ext_wid );
  /* Text TV 1 All text in one long string (without '\n's 3 for line wrapping).
  ACAD seems to add braces ({ }) and backslash-P's to indicate paragraphs
  based on the "\r\n"'s found in the imported file. But, all the text is in
  this one long string -- not broken into 1- and 3-groups as in DXF and
  entget. ACAD's entget breaks this string into 250-char pieces (not 255 as
  doc'd) – even if it's mid-word. The 1-group always gets the tag end;
  therefore, the 3's are always 250 chars long. */
  text = sBuf->getVariableText( version, false ); /* Text value TV 1 */
  if ( version > DRW::AC1014 )  //2000+
  {
    buf->getBitShort();/* Linespacing Style BS 73 */
    interlin = buf->getBitDouble();/* Linespacing Factor BD 44 */
    buf->getBit();/* Unknown bit B */
  }
  if ( version > DRW::AC1015 )  //2004+
  {
    /* Background flags BL 0 = no background, 1 = background fill, 2 =background
    fill with drawing fill color. */
    dint32 bk_flags = buf->getBitLong(); //! @todo add to DRW_MText
    if ( bk_flags == 1 )
    {
      /* Background scale factor BL Present if background flags = 1, default = 1.5*/
      buf->getBitLong();
      /* Background color CMC Present if background flags = 1 */
      buf->getCmColor( version ); //RLZ: warning CMC or ENC
      //! @todo buf->getCMC
      /* Background transparency BL Present if background flags = 1 */
      buf->getBitLong();
    }
  }

  /* Common Entity Handle Data */
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  styleH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "text style Handle:%1.%2 0x%3" )
               .arg( styleH.code ).arg( styleH.size ).arg( styleH.ref, 0, 16 )
             );

  /* CRC X --- */
  return buf->isGood();
}

void DRW_MText::updateAngle()
{
  if ( haveXAxis )
  {
    angle = std::atan2( secPoint.y, secPoint.x ) * 180 / M_PI;
  }
}

void DRW_Polyline::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 70:
      flags = reader->getInt32();
      break;
    case 40:
      defstawidth = reader->getDouble();
      break;
    case 41:
      defendwidth = reader->getDouble();
      break;
    case 71:
      vertexcount = reader->getInt32();
      break;
    case 72:
      facecount = reader->getInt32();
      break;
    case 73:
      smoothM = reader->getInt32();
      break;
    case 74:
      smoothN = reader->getInt32();
      break;
    case 75:
      curvetype = reader->getInt32();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

//0x0F polyline 2D bit 4(8) & 5(16) NOT set
//0x10 polyline 3D bit 4(8) set
//0x1D PFACE bit 5(16) set
bool DRW_Polyline::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing polyline *********************************************" );

  dint32 ooCount = 0;
  if ( oType == 0x0F ) //pline 2D
  {
    flags = buf->getBitShort();
    QgsDebugMsg( QString( "flags value:0x%1" ).arg( flags, 0, 16 ) );
    curvetype = buf->getBitShort();
    defstawidth = buf->getBitDouble();
    defendwidth = buf->getBitDouble();
    thickness = buf->getThickness( version > DRW::AC1014 );
    basePoint = DRW_Coord( 0, 0, buf->getBitDouble() );
    extPoint = buf->getExtrusion( version > DRW::AC1014 );
  }
  else if ( oType == 0x10 ) //pline 3D
  {
    duint8 tmpFlag = buf->getRawChar8();
    QgsDebugMsg( QString( "flags 1 value:0x%1" ).arg( tmpFlag, 0, 16 ) );
    if ( tmpFlag & 1 )
      curvetype = 5;
    else if ( tmpFlag & 2 )
      curvetype = 6;
    if ( tmpFlag & 3 )
    {
      curvetype = 8;
      flags |= 4;
    }
    tmpFlag = buf->getRawChar8();
    if ( tmpFlag & 1 )
      flags |= 1;
    flags |= 8; //indicate 3DPOL
    QgsDebugMsg( QString( "flags 2 value:0x%1" ).arg( tmpFlag, 0, 16 ) );
  }
  else if ( oType == 0x1D ) //PFACE
  {
    flags = 64;
    vertexcount = buf->getBitShort();
    facecount = buf->getBitShort();
    QgsDebugMsg( QString( "vertex count:%1 face count:%2 flags value:0x%3" )
                 .arg( vertexcount ).arg( facecount ).arg( flags, 0, 16 )
               );
  }
  if ( version > DRW::AC1015 )  //2004+
  {
    ooCount = buf->getBitLong();
  }

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  if ( version < DRW::AC1018 )  //2000-
  {
    dwgHandle objectH = buf->getOffsetHandle( handle );
    firstEH = objectH.ref;
    QgsDebugMsg( QString( "first vertex handle:%1.%2 0x%3" )
                 .arg( objectH.code ).arg( objectH.size ).arg( objectH.ref, 0, 16 )
               );

    objectH = buf->getOffsetHandle( handle );
    lastEH = objectH.ref;

    QgsDebugMsg( QString( "last vertex:%1, remaining bytes %2" )
                 .arg( QString( "%1.%2 0x%3" ).arg( objectH.code ).arg( objectH.size ).arg( objectH.ref, 0, 16 ) )
                 .arg( buf->numRemainingBytes() )
               );
  }
  else
  {
    for ( dint32 i = 0; i < ooCount; ++i )
    {
      dwgHandle objectH = buf->getOffsetHandle( handle );
      handleList.push_back( objectH.ref );

      QgsDebugMsg( QString( "vertex handle:%1, remaining bytes %2" )
                   .arg( QString( "%1.%2 0x%3" ).arg( objectH.code ).arg( objectH.size ).arg( objectH.ref, 0, 16 ) )
                   .arg( buf->numRemainingBytes() )
                 );
    }
  }
  seqEndH = buf->getOffsetHandle( handle );

  QgsDebugMsg( QString( "SEQEND handle:%1 remaining bytes %2" )
               .arg( QString( "%1.%2 0x%3" ).arg( seqEndH.code ).arg( seqEndH.size ).arg( seqEndH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Vertex::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 70:
      flags = reader->getInt32();
      break;
    case 40:
      stawidth = reader->getDouble();
      break;
    case 41:
      endwidth = reader->getDouble();
      break;
    case 42:
      bulge = reader->getDouble();
      break;
    case 50:
      tgdir = reader->getDouble();
      break;
    case 71:
      vindex1 = reader->getInt32();
      break;
    case 72:
      vindex2 = reader->getInt32();
      break;
    case 73:
      vindex3 = reader->getInt32();
      break;
    case 74:
      vindex4 = reader->getInt32();
      break;
    case 91:
      identifier = reader->getInt32();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

//0x0A vertex 2D
//0x0B vertex 3D
//0x0C MESH
//0x0D PFACE
//0x0E PFACE FACE
bool DRW_Vertex::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs, double el )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing pline Vertex *********************************************" );

  if ( oType == 0x0A ) //pline 2D, needed example
  {
    flags = buf->getRawChar8(); //RLZ: EC  unknown type
    basePoint = buf->get3BitDouble();
    basePoint.z = el;

    QgsDebugMsg( QString( " flags value:0x%1 basePoint:%2" )
                 .arg( flags, 0, 16 )
                 .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               );

    stawidth = buf->getBitDouble();
    if ( stawidth < 0 )
      endwidth = stawidth = std::fabs( stawidth );
    else
      endwidth = buf->getBitDouble();
    bulge = buf->getBitDouble();
    if ( version > DRW::AC1021 ) //2010+
    {
      dint32 id = buf->getBitLong();
      QgsDebugMsg( QString( " Vertex ID:%1" ).arg( id ) );
      Q_UNUSED( id );
    }
    tgdir = buf->getBitDouble();
  }
  else if ( oType == 0x0B || oType == 0x0C || oType == 0x0D ) //PFACE
  {
    flags = buf->getRawChar8(); //RLZ: EC  unknown type
    basePoint = buf->get3BitDouble();

    QgsDebugMsg( QString( " flags value:0x%1 basePoint:%2" )
                 .arg( flags, 0, 16 )
                 .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               );
  }
  else if ( oType == 0x0E ) //PFACE FACE
  {
    vindex1 = buf->getBitShort();
    vindex2 = buf->getBitShort();
    vindex3 = buf->getBitShort();
    vindex4 = buf->getBitShort();
  }

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Hatch::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 2:
      name = reader->getUtf8String();
      break;
    case 70:
      solid = reader->getInt32();
      break;
    case 71:
      associative = reader->getInt32();
      break;
    case 72:        /*edge type*/
      if ( ispol )  //if is polyline is a as_bulge flag
      {
        break;
      }
      else if ( reader->getInt32() == 1 )  //line
      {
        addLine();
      }
      else if ( reader->getInt32() == 2 )  //arc
      {
        addArc();
      }
      else if ( reader->getInt32() == 3 )  //elliptic arc
      {
        addEllipse();
      }
      else if ( reader->getInt32() == 4 )  //spline
      {
        addSpline();
      }
      break;
    case 10:
      if ( pt ) pt->basePoint.x = reader->getDouble();
      else if ( pline )
      {
        plvert = pline->addVertex();
        plvert->x = reader->getDouble();
      }
      break;
    case 20:
      if ( pt ) pt->basePoint.y = reader->getDouble();
      else if ( plvert ) plvert ->y = reader->getDouble();
      break;
    case 11:
      if ( line ) line->secPoint.x = reader->getDouble();
      else if ( ellipse ) ellipse->secPoint.x = reader->getDouble();
      break;
    case 21:
      if ( line ) line->secPoint.y = reader->getDouble();
      else if ( ellipse ) ellipse->secPoint.y = reader->getDouble();
      break;
    case 40:
      if ( arc ) arc->mRadius = reader->getDouble();
      else if ( ellipse ) ellipse->ratio = reader->getDouble();
      break;
    case 41:
      scale = reader->getDouble();
      break;
    case 42:
      if ( plvert ) plvert ->bulge = reader->getDouble();
      break;
    case 50:
      if ( arc ) arc->staangle = reader->getDouble() / ARAD;
      else if ( ellipse ) ellipse->staparam = reader->getDouble() / ARAD;
      break;
    case 51:
      if ( arc ) arc->endangle = reader->getDouble() / ARAD;
      else if ( ellipse ) ellipse->endparam = reader->getDouble() / ARAD;
      break;
    case 52:
      angle = reader->getDouble();
      break;
    case 73:
      if ( arc ) arc->isccw = reader->getInt32();
      else if ( pline ) pline->flags = reader->getInt32();
      break;
    case 75:
      hstyle = reader->getInt32();
      break;
    case 76:
      hpattern = reader->getInt32();
      break;
    case 77:
      doubleflag = reader->getInt32();
      break;
    case 78:
      deflines = reader->getInt32();
      break;
    case 91:
      loopsnum = reader->getInt32();
      RESERVE( looplist, loopsnum );
      break;
    case 92:
      loop = new DRW_HatchLoop( reader->getInt32() );
      looplist.push_back( loop );
      if ( reader->getInt32() & 2 )
      {
        ispol = true;
        clearEntities();
        pline = new DRW_LWPolyline;
        loop->objlist.push_back( pline );
      }
      else ispol = false;
      break;
    case 93:
      if ( pline )
        pline->vertexnum = reader->getInt32();
      else
        loop->numedges = reader->getInt32();//aqui reserve
      break;
    case 98: //seed points ??
      clearEntities();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}

bool DRW_Hatch::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  duint32 totalBoundItems = 0;
  bool hasPixelSize = false;

  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing hatch *********************************************" );

  //Gradient data, RLZ: is OK or if grad > 0 continue read ?
  if ( version > DRW::AC1015 ) //2004+
  {
    dint32 isGradient = buf->getBitLong();
    dint32 res = buf->getBitLong();
    double gradAngle = buf->getBitDouble();
    double gradShift = buf->getBitDouble();
    dint32 singleCol = buf->getBitLong();
    double gradTint = buf->getBitDouble();
    dint32 numCol = buf->getBitLong();

    QgsDebugMsg( QString( "is Gradient:%1 reserved:%2 Gradient angle:%3 Gradient shift:%4 single color Grad:%5 Gradient tint:%6, num colors:%7" )
                 .arg( isGradient )
                 .arg( res )
                 .arg( gradAngle )
                 .arg( gradShift )
                 .arg( singleCol )
                 .arg( gradTint )
                 .arg( numCol )
               );
    Q_UNUSED( isGradient );
    Q_UNUSED( res );
    Q_UNUSED( gradAngle );
    Q_UNUSED( gradShift );
    Q_UNUSED( singleCol );
    Q_UNUSED( gradTint );

    for ( dint32 i = 0 ; i < numCol; ++i )
    {
      double unkDouble = buf->getBitDouble();
      duint16 unkShort = buf->getBitShort();
      dint32 rgbCol = buf->getBitLong();
      duint8 ignCol = buf->getRawChar8();

      QgsDebugMsg( QString( "unkDouble:%1 unkShort:%2 rgbcolor:%3 ignoredcolor:%4" )
                   .arg( unkDouble ).arg( unkShort )
                   .arg( rgbCol )
                   .arg( ignCol )
                 );
      Q_UNUSED( unkDouble );
      Q_UNUSED( unkShort );
      Q_UNUSED( rgbCol );
      Q_UNUSED( ignCol );
    }
    UTF8STRING gradName = sBuf->getVariableText( version, false );
    QgsDebugMsg( QString( "gradient name:%1" ).arg( gradName.c_str() ) );
    Q_UNUSED( gradName );
  }
  basePoint.z = buf->getBitDouble();
  extPoint = buf->get3BitDouble();
  name = sBuf->getVariableText( version, false );
  solid = buf->getBit();
  associative = buf->getBit();
  loopsnum = buf->getBitLong();

  QgsDebugMsg( QString( "base point:%1, extrusion:%2, pattern:%3, solid:%4 associative:%5 loops:%6" )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) )
               .arg( name.c_str() )
               .arg( solid )
               .arg( associative )
               .arg( loopsnum )
             );

  //read loops
  for ( std::vector<DRW_HatchLoop *>::size_type i = 0 ; i < loopsnum && buf->isGood(); ++i )
  {
    loop = new DRW_HatchLoop( buf->getBitLong() );
    QgsDebugMsg( QString( " type: %1" ).arg( loop->type ) );
    hasPixelSize |= ( loop->type & 4 ) != 0;
    if ( !( loop->type & 2 ) )  //Not polyline
    {
      dint32 numPathSeg = buf->getBitLong();

      QgsDebugMsg( QString( "segs: %1" ).arg( numPathSeg ) );

      for ( dint32 j = 0; j < numPathSeg; ++j )
      {
        duint8 typePath = buf->getRawChar8();
        QgsDebugMsg( QString( "  typepath: %1" ).arg( typePath ) );
        if ( typePath == 1 )  //line
        {
          addLine();
          line->basePoint = buf->get2RawDouble();
          line->secPoint = buf->get2RawDouble();
        }
        else if ( typePath == 2 )  //circle arc
        {
          addArc();
          arc->basePoint = buf->get2RawDouble();
          arc->mRadius = buf->getBitDouble();
          arc->staangle = buf->getBitDouble();
          arc->endangle = buf->getBitDouble();
          arc->isccw = buf->getBit();
        }
        else if ( typePath == 3 )  //ellipse arc
        {
          addEllipse();
          ellipse->basePoint = buf->get2RawDouble();
          ellipse->secPoint = buf->get2RawDouble();
          ellipse->ratio = buf->getBitDouble();
          ellipse->staparam = buf->getBitDouble();
          ellipse->endparam = buf->getBitDouble();
          ellipse->isccw = buf->getBit();
        }
        else if ( typePath == 4 )  //spline
        {
          addSpline();
          spline->degree = buf->getBitLong();
          bool isRational = buf->getBit();
          spline->flags |= ( isRational << 2 ); //rational
          spline->flags |= ( buf->getBit() << 1 ); //periodic
          spline->nknots = buf->getBitLong();
          spline->ncontrol = buf->getBitLong();

          QgsDebugMsg( QString( "  degree:%1 flags:0x%2 nknots:%3 ncontrol:%4" )
                       .arg( spline->degree ).arg( spline->flags, 0, 16 )
                       .arg( spline->nknots ).arg( spline->ncontrol )
                     );
          RESERVE( spline->knotslist, spline->nknots );
          for ( dint32 j = 0; j < spline->nknots; ++j )
          {
            spline->knotslist.push_back( buf->getBitDouble() );
            QgsDebugMsg( QString( "  knot %1: %2" ).arg( j )
                         .arg( spline->knotslist.back() )
                       );
          }
          RESERVE( spline->controllist, spline->ncontrol );
          for ( dint32 j = 0; j < spline->ncontrol && buf->isGood(); ++j )
          {
            DRW_Coord *crd = new DRW_Coord( buf->get2RawDouble() );
            spline->controllist.push_back( crd );
            if ( isRational )
              crd->z = buf->getBitDouble(); //RLZ: investigate how store weight
            spline->controllist.push_back( crd );
            QgsDebugMsg( QString( "  control %1: %2" )
                         .arg( j )
                         .arg( QString( "%1,%2,%3" ).arg( crd->x ).arg( crd->y ).arg( crd->z ) )
                       );
          }
          if ( version > DRW::AC1021 ) //2010+
          {
            spline->nfit = buf->getBitLong();
            QgsDebugMsg( QString( " nfit:%1" ).arg( spline->nfit ) );
            RESERVE( spline->fitlist, spline->nfit );
            for ( dint32 j = 0; j < spline->nfit && buf->isGood(); ++j )
            {
              DRW_Coord *crd = new DRW_Coord( buf->get2RawDouble() );
              spline->fitlist.push_back( crd );

              QgsDebugMsg( QString( "  fit %1: %2" )
                           .arg( j )
                           .arg( QString( "%1,%2,%3" ).arg( crd->x ).arg( crd->y ).arg( crd->z ) )
                         );
            }
            spline->tgStart = buf->get2RawDouble();
            spline->tgEnd = buf->get2RawDouble();
          }
        }
      }
    }
    else   //end not pline, start polyline
    {
      pline = new DRW_LWPolyline;
      bool hasBulges = buf->getBit();
      pline->flags = buf->getBit();//closed bit
      dint32 numVert = buf->getBitLong();

      QgsDebugMsg( QString( "hasBulge:%1 flags:%2 verts:%3" )
                   .arg( hasBulges ).arg( pline->flags, 0, 16 ).arg( numVert )
                 );

      for ( dint32 j = 0; j < numVert && buf->isGood(); ++j )
      {
        DRW_Vertex2D v;
        v.x = buf->getRawDouble();
        v.y = buf->getRawDouble();
        if ( hasBulges )
          v.bulge = buf->getBitDouble();
        pline->addVertex( v );
      }
      loop->objlist.push_back( pline );
    }//end polyline
    loop->update();
    looplist.push_back( loop );
    totalBoundItems += buf->getBitLong();

    QgsDebugMsg( QString( " totalBoundItems:%1" ).arg( totalBoundItems ) );
  } //end read loops

  hstyle = buf->getBitShort();
  hpattern = buf->getBitShort();

  QgsDebugMsg( QString( "hatch style:%1 pattern type:%2" ).arg( hstyle ).arg( hpattern ) );
  if ( !solid )
  {
    angle = buf->getBitDouble();
    scale = buf->getBitDouble();
    doubleflag = buf->getBit();
    deflines = buf->getBitShort();

    QgsDebugMsg( QString( "angle:%1 scale:%2 double:%3 deflines:%4" )
                 .arg( angle ).arg( scale )
                 .arg( doubleflag ).arg( deflines )
               );
    Q_UNUSED( angle );
    Q_UNUSED( scale );

    for ( dint32 i = 0 ; i < deflines && buf->isGood(); ++i )
    {
      DRW_Coord ptL, offL;
      double angleL = buf->getBitDouble();
      ptL.x = buf->getBitDouble();
      ptL.y = buf->getBitDouble();
      offL.x = buf->getBitDouble();
      offL.y = buf->getBitDouble();
      duint16 numDashL = buf->getBitShort();

      QgsDebugMsg( QString( "def line: a=%1 pt=%2 off=%3" )
                   .arg( angleL )
                   .arg( QString( "%1,%2" ).arg( ptL.x ).arg( ptL.y ) )
                   .arg( QString( "%1,%2" ).arg( offL.x ).arg( offL.y ) )
                 );
      Q_UNUSED( angleL );
      Q_UNUSED( ptL );
      Q_UNUSED( offL );

      for ( duint16 j = 0 ; j < numDashL; ++j )
      {
        double lengthL = buf->getBitDouble();
        QgsDebugMsg( QString( " %1: %2" ).arg( j ).arg( lengthL ) );
        Q_UNUSED( lengthL );
      }
    }//end deflines
  } //end not solid

  if ( hasPixelSize )
  {
    ddouble64 pixsize = buf->getBitDouble();
    QgsDebugMsg( QString( "pixel size:%1" ).arg( pixsize ) );
    Q_UNUSED( pixsize );
  }

  dint32 numSeedPoints = buf->getBitLong();
  QgsDebugMsg( QString( "num Seed Points %1" ).arg( numSeedPoints ) );
  //read Seed Points
  DRW_Coord seedPt;
  for ( dint32 i = 0 ; i < numSeedPoints && buf->isGood(); ++i )
  {
    seedPt.x = buf->getRawDouble();
    seedPt.y = buf->getRawDouble();
    QgsDebugMsg( QString( " %1: %2,%3" ).arg( i ).arg( seedPt.x ).arg( seedPt.y ) );
  }

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  QgsDebugMsg( QString( "Remaining bytes:%1" ).arg( buf->numRemainingBytes() ) );

  for ( duint32 i = 0 ; i < totalBoundItems && buf->isGood(); ++i )
  {
    dwgHandle biH = buf->getHandle();
    QgsDebugMsg( QString( "Boundary Items Handle:%1.%2 0x%3" ).arg( biH.code ).arg( biH.size ).arg( biH.ref, 0, 16 ) );
  }
  QgsDebugMsg( QString( "Remaining bytes:%1" ).arg( buf->numRemainingBytes() ) );

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Spline::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 210:
      normalVec.x = reader->getDouble();
      break;
    case 220:
      normalVec.y = reader->getDouble();
      break;
    case 230:
      normalVec.z = reader->getDouble();
      break;
    case 12:
      tgStart.x = reader->getDouble();
      break;
    case 22:
      tgStart.y = reader->getDouble();
      break;
    case 32:
      tgStart.z = reader->getDouble();
      break;
    case 13:
      tgEnd.x = reader->getDouble();
      break;
    case 23:
      tgEnd.y = reader->getDouble();
      break;
    case 33:
      tgEnd.z = reader->getDouble();
      break;
    case 70:
      flags = reader->getInt32();
      break;
    case 71:
      degree = reader->getInt32();
      break;
    case 72:
      nknots = reader->getInt32();
      break;
    case 73:
      ncontrol = reader->getInt32();
      break;
    case 74:
      nfit = reader->getInt32();
      break;
    case 42:
      tolknot = reader->getDouble();
      break;
    case 43:
      tolcontrol = reader->getDouble();
      break;
    case 44:
      tolfit = reader->getDouble();
      break;
    case 10:
    {
      controlpoint = new DRW_Coord();
      controllist.push_back( controlpoint );
      controlpoint->x = reader->getDouble();
      break;
    }
    case 20:
      if ( controlpoint )
        controlpoint->y = reader->getDouble();
      break;
    case 30:
      if ( controlpoint )
        controlpoint->z = reader->getDouble();
      break;
    case 11:
    {
      fitpoint = new DRW_Coord();
      fitlist.push_back( fitpoint );
      fitpoint->x = reader->getDouble();
      break;
    }
    case 21:
      if ( fitpoint )
        fitpoint->y = reader->getDouble();
      break;
    case 31:
      if ( fitpoint )
        fitpoint->z = reader->getDouble();
      break;
    case 40:
      knotslist.push_back( reader->getDouble() );
      break;
//    case 41:
//        break;
    default:
      DRW_Entity::parseCode( code, reader );
      break;
  }
}

bool DRW_Spline::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  bool ret = DRW_Entity::parseDwg( version, buf, nullptr, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing spline *********************************************" );

  duint8 weight = 0; // RLZ ??? flags, weight, code 70, bit 4 (16)

  dint32 scenario = buf->getBitLong();

  QgsDebugMsg( QString( "scenario: %1" ).arg( scenario ) );

  if ( version > DRW::AC1024 )
  {
    dint32 splFlag1 = buf->getBitLong();
    if ( splFlag1 & 1 )
      scenario = 2;
    dint32 knotParam = buf->getBitLong();

    QgsDebugMsg( QString( "2013 splFlag1:%1, 2013 knotParam:%2" ).arg( splFlag1 ).arg( knotParam ) );
    Q_UNUSED( knotParam );
    // QgsDebugMsg( QString( "unk bit:%1").arg( buf->getBit()) );
  }

  degree = buf->getBitLong(); //RLZ: code 71, verify with dxf
  QgsDebugMsg( QString( " degree:%1" ).arg( degree ) );

  if ( scenario == 2 )
  {
    flags = 8;//scenario 2 = not rational & planar
    tolfit = buf->getBitDouble();//BD
    tgStart = buf->get3BitDouble();
    tgEnd = buf->get3BitDouble();
    nfit = buf->getBitLong();

    QgsDebugMsg( QString( "flags: 0x%1, tolfit:%2, tangent start:%3 end:%4 nfit:%5" )
                 .arg( flags, 0, 16 ).arg( tolfit )
                 .arg( QString( "%1,%2,%3" ).arg( tgStart.x ).arg( tgStart.y ).arg( tgStart.z ) )
                 .arg( QString( "%1,%2,%3" ).arg( tgEnd.x ).arg( tgEnd.y ).arg( tgEnd.z ) )
                 .arg( nfit )
               );
  }
  else if ( scenario == 1 )
  {
    flags = 8; //scenario 1 = rational & planar
    flags |= buf->getBit() << 2; //flags, rational, code 70, bit 2 (4)
    flags |= buf->getBit(); //flags, closed, code 70, bit 0 (1)
    flags |= buf->getBit() << 1; //flags, periodic, code 70, bit 1 (2)
    tolknot = buf->getBitDouble();
    tolcontrol = buf->getBitDouble();
    nknots = buf->getBitLong();
    ncontrol = buf->getBitLong();
    weight = buf->getBit(); // RLZ ??? flags, weight, code 70, bit 4 (16)

    QgsDebugMsg( QString( "flags:0x%1, knot tolerance:%2, control point tolerance:%3, num knots:%4, num control pt:%5, weight:%6" )
                 .arg( flags, 0, 16 ).arg( tolknot ).arg( tolcontrol ).arg( nknots ).arg( ncontrol ).arg( weight )
               );
  }
  else
  {
    QgsDebugMsg( QString( "spline, unknown scenario %1" ).arg( scenario ) );
    return false; //RLZ: from doc only 1 or 2 are OK ?
  }

  RESERVE( knotslist, nknots );
  for ( dint32 i = 0; i < nknots; ++i )
  {
    double d = buf->getBitDouble();
    knotslist.push_back( d );
    QgsDebugMsgLevel( QString( "knot %1: %2 rem=%3" )
                      .arg( i ).arg( d, 0, 'g', 17 ).arg( buf->numRemainingBytes() ), 4
                    );
  }

  RESERVE( controllist, ncontrol );
  for ( dint32 i = 0; i < ncontrol; ++i )
  {
    DRW_Coord *crd = new DRW_Coord( buf->get3BitDouble() );
    controllist.push_back( crd );
    QgsDebugMsgLevel( QString( "cp %1: %2,%3,%4 rem:%5" )
                      .arg( i ).arg( crd->x, 0, 'g', 17 ).arg( crd->y, 0, 'g', 17 ).arg( crd->z, 0, 'g', 17 ).arg( buf->numRemainingBytes() ), 4
                    );
    if ( weight )
    {
      double w = buf->getBitDouble(); //RLZ Warning: D (BD or RD)
      QgsDebugMsgLevel( QString( "weight %1: %2 rem:%3" )
                        .arg( i ).arg( w, 0, 'g', 17 ).arg( buf->numRemainingBytes() ), 4
                      );
      Q_UNUSED( w );
    }
  }

  RESERVE( fitlist, nfit );
  for ( dint32 i = 0; i < nfit; ++i )
  {
    DRW_Coord *crd = new DRW_Coord( buf->get3BitDouble() );
    fitlist.push_back( crd );
    QgsDebugMsgLevel( QString( "fp %1: %2,%3,%4 rem:%5" )
                      .arg( i ).arg( crd->x, 0, 'g', 17 ).arg( crd->y, 0, 'g', 17 ).arg( crd->z, 0, 'g', 17 ).arg( buf->numRemainingBytes() ), 4
                    );
  }

  /* Common Entity Handle Data */
  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

//    RS crc;   //RS */
  return buf->isGood();
}

void DRW_Image::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 12:
      vVector.x = reader->getDouble();
      break;
    case 22:
      vVector.y = reader->getDouble();
      break;
    case 32:
      vVector.z = reader->getDouble();
      break;
    case 13:
      sizeu = reader->getDouble();
      break;
    case 23:
      sizev = reader->getDouble();
      break;
    case 340:
      ref = reader->getHandleString();
      break;
    case 280:
      clip = reader->getInt32();
      break;
    case 281:
      brightness = reader->getInt32();
      break;
    case 282:
      contrast = reader->getInt32();
      break;
    case 283:
      fade = reader->getInt32();
      break;
    default:
      DRW_Line::parseCode( code, reader );
      break;
  }
}

bool DRW_Image::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing image *********************************************" );

  dint32 classVersion = buf->getBitLong();
  basePoint = buf->get3BitDouble();
  secPoint = buf->get3BitDouble();
  vVector = buf->get3BitDouble();
  sizeu = buf->getRawDouble();
  sizev = buf->getRawDouble();

  QgsDebugMsg( QString( "class version:%1, base point:%2, U:%3, V:%4, size U:%5, V:%6" )
               .arg( classVersion )
               .arg( QString( "%1,%2,%3" ).arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( secPoint.x ).arg( secPoint.y ).arg( secPoint.z ) )
               .arg( QString( "%1,%2,%3" ).arg( vVector.x ).arg( vVector.y ).arg( vVector.z ) )
               .arg( sizeu ).arg( sizev )
             );
  Q_UNUSED( classVersion );

  duint16 displayProps = buf->getBitShort();
  DRW_UNUSED( displayProps );//RLZ: temporary, complete API
  clip = buf->getBit();
  brightness = buf->getRawChar8();
  contrast = buf->getRawChar8();
  fade = buf->getRawChar8();
  if ( version > DRW::AC1021 )  //2010+
  {
    bool clipMode = buf->getBit();
    DRW_UNUSED( clipMode );//RLZ: temporary, complete API
  }
  duint16 clipType = buf->getBitShort();
  if ( clipType == 1 )
  {
    buf->get2RawDouble();
    buf->get2RawDouble();
  }
  else   //clipType == 2
  {
    dint32 numVerts = buf->getBitLong();
    for ( int i = 0; i < numVerts; ++i )
      buf->get2RawDouble();
  }

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  dwgHandle biH = buf->getHandle();

  QgsDebugMsg( QString( "ImageDef handle:%1.%2 0x%3" ).arg( biH.code ).arg( biH.size ).arg( biH.ref, 0, 16 ) );

  ref = biH.ref;
  biH = buf->getHandle();

  QgsDebugMsg( QString( "ImageDefReactor handle:%1.%2 0x%3" ).arg( biH.code ).arg( biH.size ).arg( biH.ref, 0, 16 ) );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  // RS crc;   //RS */

  return buf->isGood();
}

void DRW_Dimension::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 1:
      text = reader->getUtf8String();
      break;
    case 2:
      name = reader->getString();
      break;
    case 3:
      style = reader->getUtf8String();
      break;
    case 70:
      type = reader->getInt32();
      break;
    case 71:
      align = reader->getInt32();
      break;
    case 72:
      linesty = reader->getInt32();
      break;
    case 10:
      defPoint.x = reader->getDouble();
      break;
    case 20:
      defPoint.y = reader->getDouble();
      break;
    case 30:
      defPoint.z = reader->getDouble();
      break;
    case 11:
      textPoint.x = reader->getDouble();
      break;
    case 21:
      textPoint.y = reader->getDouble();
      break;
    case 31:
      textPoint.z = reader->getDouble();
      break;
    case 12:
      clonePoint.x = reader->getDouble();
      break;
    case 22:
      clonePoint.y = reader->getDouble();
      break;
    case 32:
      clonePoint.z = reader->getDouble();
      break;
    case 13:
      def1.x = reader->getDouble();
      break;
    case 23:
      def1.y = reader->getDouble();
      break;
    case 33:
      def1.z = reader->getDouble();
      break;
    case 14:
      def2.x = reader->getDouble();
      break;
    case 24:
      def2.y = reader->getDouble();
      break;
    case 34:
      def2.z = reader->getDouble();
      break;
    case 15:
      circlePoint.x = reader->getDouble();
      break;
    case 25:
      circlePoint.y = reader->getDouble();
      break;
    case 35:
      circlePoint.z = reader->getDouble();
      break;
    case 16:
      arcPoint.x = reader->getDouble();
      break;
    case 26:
      arcPoint.y = reader->getDouble();
      break;
    case 36:
      arcPoint.z = reader->getDouble();
      break;
    case 41:
      linefactor = reader->getDouble();
      break;
    case 53:
      rot = reader->getDouble();
      break;
    case 50:
      angle = reader->getDouble();
      break;
    case 52:
      oblique = reader->getDouble();
      break;
    case 40:
      length = reader->getDouble();
      break;
    case 51:
      hdir = reader->getDouble();
      break;
    default:
      DRW_Entity::parseCode( code, reader );
      break;
  }
}

bool DRW_Dimension::parseDwg( DRW::Version version, dwgBuffer *buf, dwgBuffer *sBuf )
{
  QgsDebugMsg( "***************************** parsing dimension *********************************************" );

  if ( version > DRW::AC1021 ) //2010+
  {
    duint8 dimVersion = buf->getRawChar8();
    QgsDebugMsg( QString( "dimVersion:%1" ).arg( dimVersion ) );
    Q_UNUSED( dimVersion );
  }
  extPoint = buf->getExtrusion( version > DRW::AC1014 );

  QgsDebugMsg( QString( "extrusion:%1,%2,%3" ).arg( extPoint.x ).arg( extPoint.y ).arg( extPoint.z ) );

  if ( version > DRW::AC1014 ) //2000+
  {
    int bit0 = buf->getBit();
    int bit1 = buf->getBit();
    int bit2 = buf->getBit();
    int bit3 = buf->getBit();
    int bit4 = buf->getBit();

    QgsDebugMsg( QString( "Five unknown bits: %1%2%3%4%5" ).arg( bit0 ).arg( bit1 ).arg( bit2 ).arg( bit3 ).arg( bit4 ) );
    Q_UNUSED( bit0 );
    Q_UNUSED( bit1 );
    Q_UNUSED( bit2 );
    Q_UNUSED( bit3 );
    Q_UNUSED( bit4 );
  }
  textPoint.x = buf->getRawDouble();
  textPoint.y = buf->getRawDouble();
  textPoint.z = buf->getBitDouble();

  QgsDebugMsg( QString( "textPoint:%1,%2,%3" ).arg( textPoint.x ).arg( textPoint.y ).arg( textPoint.z ) );

  type = buf->getRawChar8();

  QgsDebugMsg( QString( "type (70) read: %1" ).arg( type ) );
  type = ( type & 1 ) ? type & 0x7F : type | 0x80; //set bit 7
  type = ( type & 2 ) ? type | 0x20 : type & 0xDF; //set bit 5
  QgsDebugMsg( QString( "type (70) set: %1" ).arg( type ) );

  //clear last 3 bits to set integer dim type
  type &= 0xF8;
  text = sBuf->getVariableText( version, false );

  QgsDebugMsg( QString( "forced dim text:%1" ).arg( text.c_str() ) );
  rot = buf->getBitDouble();
  hdir = buf->getBitDouble();
  DRW_Coord inspoint = buf->get3BitDouble();
  double insRot_code54 = buf->getBitDouble(); //RLZ: unknown, investigate

  QgsDebugMsg( QString( "insPoint:%1 insRot_code54:%4" )
               .arg( QString( "%1,%2,%3" ).arg( inspoint.x ).arg( inspoint.y ).arg( inspoint.z ) )
               .arg( insRot_code54 )
             );
  Q_UNUSED( inspoint );
  Q_UNUSED( insRot_code54 );

  if ( version > DRW::AC1014 ) //2000+
  {
    align = buf->getBitShort();
    linesty = buf->getBitShort();
    linefactor = buf->getBitDouble();
    double actMeas = buf->getBitDouble();

    QgsDebugMsg( QString( " actMeas_code42: %1" ).arg( actMeas ) );
    Q_UNUSED( actMeas );

    if ( version > DRW::AC1018 ) //2007+
    {
      bool unk = buf->getBit();
      bool flip1 = buf->getBit();
      bool flip2 = buf->getBit();

      QgsDebugMsg( QString( "2007, unk, flip1, flip2: %1, %2, %3" )
                   .arg( unk ).arg( flip1 ).arg( flip2 )
                 );
      Q_UNUSED( unk );
      Q_UNUSED( flip1 );
      Q_UNUSED( flip2 );
    }
  }
  clonePoint.x = buf->getRawDouble();
  clonePoint.y = buf->getRawDouble();

  QgsDebugMsg( QString( "clonePoint:%1,%2,%3" ).arg( clonePoint.x ).arg( clonePoint.y ).arg( clonePoint.z ) );

  return buf->isGood();
}

bool DRW_DimAligned::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;
  if ( oType == 0x15 )
  {
    QgsDebugMsg( "***************************** parsing dim linear *********************************************" );
  }
  else
  {
    QgsDebugMsg( "***************************** parsing dim aligned *********************************************" );
  }
  DRW_Coord pt = buf->get3BitDouble();
  setPt3( pt ); //def1

  QgsDebugMsg( QString( "def1: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt4( pt );

  QgsDebugMsg( QString( "def2: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setDefPoint( pt );

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  setOb52( buf->getBitDouble() );
  if ( oType == 0x15 )
    setAn50( buf->getBitDouble() * ARAD );
  else
    type |= 1;

  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */

  return buf->isGood();
}

bool DRW_DimRadial::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing dim radial *********************************************" );

  DRW_Coord pt = buf->get3BitDouble();
  setDefPoint( pt ); //code 10

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt5( pt ); //center pt  code 15

  QgsDebugMsg( QString( "center point: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  setRa40( buf->getBitDouble() ); //leader length code 40
  QgsDebugMsg( QString( "leader length: %1" ).arg( getRa40() ) );
  type |= 4;

  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */
  return buf->isGood();
}

bool DRW_DimDiametric::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing dim diametric *********************************************" );

  DRW_Coord pt = buf->get3BitDouble();
  setPt5( pt ); //center pt  code 15

  QgsDebugMsg( QString( "center point: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setDefPoint( pt ); //code 10

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  setRa40( buf->getBitDouble() ); //leader length code 40
  QgsDebugMsg( QString( "leader length: %1" ).arg( getRa40() ) );

  type |= 3;
  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );
  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */
  return buf->isGood();
}

bool DRW_DimAngular::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing dim angular *********************************************" );

  DRW_Coord pt;
  pt.x = buf->getRawDouble();
  pt.y = buf->getRawDouble();
  setPt6( pt ); //code 16

  QgsDebugMsg( QString( "arc point: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt3( pt ); //def1  code 13

  QgsDebugMsg( QString( "def1: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt4( pt ); //def2  code 14

  QgsDebugMsg( QString( "def2: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt5( pt ); //center pt  code 15

  QgsDebugMsg( QString( "center point: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setDefPoint( pt ); //code 10

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  type |= 0x02;

  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */
  return buf->isGood();
}

bool DRW_DimAngular3p::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing dim angular3p *********************************************" );

  DRW_Coord pt = buf->get3BitDouble();
  setDefPoint( pt ); //code 10

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt3( pt ); //def1  code 13

  QgsDebugMsg( QString( "def1: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt4( pt ); //def2  code 14

  QgsDebugMsg( QString( "def2: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt5( pt ); //center pt  code 15

  QgsDebugMsg( QString( "center point: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  type |= 0x05;
  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */
  return buf->isGood();
}

bool DRW_DimOrdinate::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;
  ret = DRW_Dimension::parseDwg( version, buf, sBuf );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing dim ordinate *********************************************" );

  DRW_Coord pt = buf->get3BitDouble();
  setDefPoint( pt );

  QgsDebugMsg( QString( "defPoint: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt3( pt ); //def1

  QgsDebugMsg( QString( "def1: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  pt = buf->get3BitDouble();
  setPt4( pt );

  QgsDebugMsg( QString( "def2: %1,%2,%3" ).arg( pt.x ).arg( pt.y ).arg( pt.z ) );

  duint8 type2 = buf->getRawChar8();//RLZ: correct this

  QgsDebugMsg( QString( "type2 (70) read: %1" ).arg( type2 ) );

  type = ( type2 & 1 ) ? type | 0x80 : type & 0xBF; //set bit 6

  QgsDebugMsg( QString( "type (70) set: %1" ).arg( type ) );

  type |= 6;

  QgsDebugMsg( QString( "type (70) final: %1" ).arg( type ) );

  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  dimStyleH = buf->getHandle();
  blockH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "dim style Handle:%1, anon block handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( blockH.code ).arg( blockH.size ).arg( blockH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */
  return buf->isGood();
}

void DRW_Leader::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 3:
      style = reader->getUtf8String();
      break;
    case 71:
      arrow = reader->getInt32();
      break;
    case 72:
      leadertype = reader->getInt32();
      break;
    case 73:
      flag = reader->getInt32();
      break;
    case 74:
      hookline = reader->getInt32();
      break;
    case 75:
      hookflag = reader->getInt32();
      break;
    case 76:
      vertnum = reader->getInt32();
      break;
    case 77:
      coloruse = reader->getInt32();
      break;
    case 40:
      textheight = reader->getDouble();
      break;
    case 41:
      textwidth = reader->getDouble();
      break;
    case 10:
    {
      vertexpoint = new DRW_Coord();
      vertexlist.push_back( vertexpoint );
      vertexpoint->x = reader->getDouble();
      break;
    }
    case 20:
      if ( vertexpoint )
        vertexpoint->y = reader->getDouble();
      break;
    case 30:
      if ( vertexpoint )
        vertexpoint->z = reader->getDouble();
      break;
    case 340:
      annotHandle = reader->getHandleString();
      break;
    case 210:
      extrusionPoint.x = reader->getDouble();
      break;
    case 220:
      extrusionPoint.y = reader->getDouble();
      break;
    case 230:
      extrusionPoint.z = reader->getDouble();
      break;
    case 211:
      horizdir.x = reader->getDouble();
      break;
    case 221:
      horizdir.y = reader->getDouble();
      break;
    case 231:
      horizdir.z = reader->getDouble();
      break;
    case 212:
      offsetblock.x = reader->getDouble();
      break;
    case 222:
      offsetblock.y = reader->getDouble();
      break;
    case 232:
      offsetblock.z = reader->getDouble();
      break;
    case 213:
      offsettext.x = reader->getDouble();
      break;
    case 223:
      offsettext.y = reader->getDouble();
      break;
    case 233:
      offsettext.z = reader->getDouble();
      break;
    default:
      DRW_Entity::parseCode( code, reader );
      break;
  }
}

bool DRW_Leader::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing leader *********************************************" );

  int bit0 = buf->getBit();
  int annot = buf->getBitShort();
  int pathtype = buf->getBitShort();
  dint32 nPt = buf->getBitLong();

  QgsDebugMsg( QString( "unknown:%1 annottype:%2 pathtype:%3 numpts:%4" )
               .arg( bit0 ).arg( annot ).arg( pathtype ).arg( nPt )
             );
  Q_UNUSED( bit0 );
  Q_UNUSED( annot )
  Q_UNUSED( pathtype )

  // add vertices
  for ( int i = 0; i < nPt; i++ )
  {
    DRW_Coord *vertex = new DRW_Coord( buf->get3BitDouble() );
    vertexlist.push_back( vertex );
    QgsDebugMsg( QString( " vertex %1: %2,%3,%4" ).arg( i ).arg( vertex->x ).arg( vertex->y ).arg( vertex->z ) );
  }

  DRW_Coord Endptproj = buf->get3BitDouble();
  QgsDebugMsg( QString( " endptproj: %1,%2,%3" ).arg( Endptproj.x ).arg( Endptproj.y ).arg( Endptproj.z ) );
  Q_UNUSED( Endptproj );

  extrusionPoint = buf->getExtrusion( version > DRW::AC1014 );
  QgsDebugMsg( QString( " extrusion: %1,%2,%3" ).arg( extrusionPoint.x ).arg( extrusionPoint.y ).arg( extrusionPoint.z ) );

  if ( version > DRW::AC1014 ) //2000+
  {
    int bit0 = buf->getBit();
    int bit1 = buf->getBit();
    int bit2 = buf->getBit();
    int bit3 = buf->getBit();
    int bit4 = buf->getBit();

    QgsDebugMsg( QString( "Five unknown bits: %1%2%3%4%5" ).arg( bit0 ).arg( bit1 ).arg( bit2 ).arg( bit3 ).arg( bit4 ) );
    Q_UNUSED( bit0 );
    Q_UNUSED( bit1 );
    Q_UNUSED( bit2 );
    Q_UNUSED( bit3 );
    Q_UNUSED( bit4 );
  }
  horizdir = buf->get3BitDouble();
  offsetblock = buf->get3BitDouble();

  QgsDebugMsg( QString( " horizdir: %1,%2,%3" ).arg( horizdir.x ).arg( horizdir.y ).arg( horizdir.z ) );
  QgsDebugMsg( QString( " offsetblock: %1,%2,%3" ).arg( offsetblock.x ).arg( offsetblock.y ).arg( offsetblock.z ) );

  if ( version > DRW::AC1012 ) //R14+
  {
    DRW_Coord unk = buf->get3BitDouble();
    QgsDebugMsg( QString( " unknown: %1,%2,%3" ).arg( unk.x ).arg( unk.y ).arg( unk.z ) );
    Q_UNUSED( unk );
  }
  if ( version < DRW::AC1015 ) //R14 -
  {
    double dimgap = buf->getBitDouble();
    QgsDebugMsg( QString( "dimgap %1" ).arg( dimgap ) );
    Q_UNUSED( dimgap );
  }
  if ( version < DRW::AC1024 ) //2010-
  {
    textheight = buf->getBitDouble();
    textwidth = buf->getBitDouble();

    QgsDebugMsg( QString( "textheight:%1 textwidth:%2" ).arg( textheight ).arg( textwidth ) );
  }
  hookline = buf->getBit();
  arrow = buf->getBit();

  QgsDebugMsg( QString( "hookline:%1 arrow flag:%2" ).arg( hookline ).arg( arrow ) );

  if ( version < DRW::AC1015 ) //R14 -
  {
    int headtype = buf->getBitShort();
    double dimasz = buf->getBitDouble();
    int unk0 = buf->getBit();
    int unk1 = buf->getBit();
    int unk2 = buf->getBitShort();
    int byblockcol = buf->getBitShort();
    int unk3 = buf->getBit();
    int unk4 = buf->getBit();

    QgsDebugMsg( QString( "Arrow head type:%1 dimasz:%2 unk0:%3 unk1:%4 unk2:%5 byblockcol:%6 unk3:%7 unk3:%9" )
                 .arg( headtype ).arg( dimasz ).arg( unk0 ).arg( unk1 ).arg( unk2 ).arg( byblockcol ).arg( unk3 ).arg( unk4 )
               );
    Q_UNUSED( headtype );
    Q_UNUSED( dimasz );
    Q_UNUSED( unk0 );
    Q_UNUSED( unk1 );
    Q_UNUSED( unk2 );
    Q_UNUSED( byblockcol );
    Q_UNUSED( unk3 );
    Q_UNUSED( unk4 );
  }
  else   //R2000+
  {
    int unk0 = buf->getBitShort();
    int unk1 = buf->getBit();
    int unk2 = buf->getBit();
    QgsDebugMsg( QString( "unk0:%1 unk1:%2 unk2:%3" ).arg( unk0 ).arg( unk1 ).arg( unk2 ) );
    Q_UNUSED( unk0 );
    Q_UNUSED( unk1 );
    Q_UNUSED( unk2 );
  }

  ret = DRW_Entity::parseDwgEntHandle( version, buf );
  if ( !ret )
    return ret;

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  AnnotH = buf->getHandle();
  annotHandle = AnnotH.ref;
  dimStyleH = buf->getHandle(); /* H 7 STYLE (hard pointer) */

  QgsDebugMsg( QString( "annot block Handle:%1, dim style handle:%2, remaining bytes %3" )
               .arg( QString( "%1.%2 0x%3" ).arg( AnnotH.code ).arg( AnnotH.size ).arg( AnnotH.ref, 0, 16 ) )
               .arg( QString( "%1.%2 0x%3" ).arg( dimStyleH.code ).arg( dimStyleH.size ).arg( dimStyleH.ref, 0, 16 ) )
               .arg( buf->numRemainingBytes() )
             );

  //    RS crc;   //RS */

  return buf->isGood();
}

void DRW_Viewport::parseCode( int code, dxfReader *reader )
{
  switch ( code )
  {
    case 40:
      pswidth = reader->getDouble();
      break;
    case 41:
      psheight = reader->getDouble();
      break;
    case 68:
      vpstatus = reader->getInt32();
      break;
    case 69:
      vpID = reader->getInt32();
      break;
    case 12:
    {
      centerPX = reader->getDouble();
      break;
    }
    case 22:
      centerPY = reader->getDouble();
      break;
    default:
      DRW_Point::parseCode( code, reader );
      break;
  }
}
//ex 22 dec 34
bool DRW_Viewport::parseDwg( DRW::Version version, dwgBuffer *buf, duint32 bs )
{
  dwgBuffer sBuff = *buf;
  dwgBuffer *sBuf = buf;
  if ( version > DRW::AC1018 )  //2007+
  {
    sBuf = &sBuff; //separate buffer for strings
  }
  bool ret = DRW_Entity::parseDwg( version, buf, sBuf, bs );
  if ( !ret )
    return ret;

  QgsDebugMsg( "***************************** parsing viewport *****************************************" );

  basePoint.x = buf->getBitDouble();
  basePoint.y = buf->getBitDouble();
  basePoint.z = buf->getBitDouble();
  pswidth = buf->getBitDouble();
  psheight = buf->getBitDouble();

  QgsDebugMsg( QString( "center: %1,%2,%3 width:%4 height:%5" )
               .arg( basePoint.x ).arg( basePoint.y ).arg( basePoint.z )
               .arg( pswidth ).arg( psheight )
             );

  //RLZ TODO: complete in dxf
  if ( version > DRW::AC1014 )  //2000+
  {
    viewTarget.x = buf->getBitDouble();
    viewTarget.y = buf->getBitDouble();
    viewTarget.z = buf->getBitDouble();

    QgsDebugMsg( QString( "viewTarget: %1,%2,%3" ).arg( viewTarget.x ).arg( viewTarget.y ).arg( viewTarget.z ) );

    viewDir.x = buf->getBitDouble();
    viewDir.y = buf->getBitDouble();
    viewDir.z = buf->getBitDouble();

    QgsDebugMsg( QString( "viewDir: %1,%2,%3" ).arg( viewDir.x ).arg( viewDir.y ).arg( viewDir.z ) );

    twistAngle = buf->getBitDouble();
    viewHeight = buf->getBitDouble();
    viewLength = buf->getBitDouble();
    frontClip = buf->getBitDouble();
    backClip = buf->getBitDouble();
    snapAngle = buf->getBitDouble();

    QgsDebugMsg( QString( "View twist Angle:%1 Height:%2 LensLength:%3 front Clip Z:%4 back Clip Z:%5 snap Angle:%6" )
                 .arg( twistAngle ).arg( viewHeight ).arg( viewLength ).arg( frontClip ).arg( backClip ).arg( snapAngle )
               );

    centerPX = buf->getRawDouble();
    centerPY = buf->getRawDouble();

    QgsDebugMsg( QString( "viewCenter: %1,%2" ).arg( centerPX ).arg( centerPY ) );

    snapPX = buf->getRawDouble();
    snapPY = buf->getRawDouble();

    QgsDebugMsg( QString( "snapBase: %1,%2" ).arg( snapPX ).arg( snapPY ) );

    snapSpPX = buf->getRawDouble();
    snapSpPY = buf->getRawDouble();

    QgsDebugMsg( QString( "snapSpacing: %1,%2" ).arg( snapSpPX ).arg( snapSpPY ) );

    //RLZ: need to complete
    double gridX = buf->getRawDouble();
    double gridY = buf->getRawDouble();
    int czoom = buf->getBitShort();

    QgsDebugMsg( QString( "gridSpacing: %1,%2 Circle zoom?: %3" ).arg( gridX ).arg( gridY ).arg( czoom ) );
    Q_UNUSED( gridX );
    Q_UNUSED( gridY );
    Q_UNUSED( czoom );
  }
  if ( version > DRW::AC1018 )  //2007+
  {
    int gridmajor = buf->getBitShort();
    QgsDebugMsg( QString( "Grid major?: %1" ).arg( gridmajor ) );
    Q_UNUSED( gridmajor );
  }
  if ( version > DRW::AC1014 )  //2000+
  {
    frozenLyCount = buf->getBitLong();
    QgsDebugMsg( QString( "Frozen Layer count?: %1" ).arg( frozenLyCount ) );

    int t = buf->getBitLong();
    QgsDebugMsg( QString( "Status Flags?: %1" ).arg( t ) );
    Q_UNUSED( t );

    //RLZ: Warning needed separate string bufer
    std::string txt = sBuf->getVariableText( version, false );
    QgsDebugMsg( QString( "Style sheet?: %1" ).arg( txt.c_str() ) );
    Q_UNUSED( txt );

    t = buf->getRawChar8();
    QgsDebugMsg( QString( "Render mode?: %1" ).arg( t ) );
    t = buf->getBit();
    QgsDebugMsg( QString( "UCS OMore...: %1" ).arg( t ) );
    t = buf->getBit();
    QgsDebugMsg( QString( "UCS VMore...: %1" ).arg( t ) );

    double x, y, z;
    x = buf->getBitDouble();
    y = buf->getBitDouble();
    z = buf->getBitDouble();
    QgsDebugMsg( QString( "UCS OMode: %1,%2,%3" ).arg( x ).arg( y ).arg( z ) );
    Q_UNUSED( x );
    Q_UNUSED( y );
    Q_UNUSED( z );

    x = buf->getBitDouble();
    y = buf->getBitDouble();
    z = buf->getBitDouble();
    QgsDebugMsg( QString( "UCS XAMode: %1,%2,%3" ).arg( x ).arg( y ).arg( z ) );

    x = buf->getBitDouble();
    y = buf->getBitDouble();
    z = buf->getBitDouble();
    QgsDebugMsg( QString( "UCS YMode: %1,%2,%3" ).arg( x ).arg( y ).arg( z ) );

    x =  buf->getBitDouble();
    QgsDebugMsg( QString( "UCS EMore: %1" ).arg( x ) );

    t = buf->getBitShort();
    QgsDebugMsg( QString( "UCS OVMore: %1" ).arg( t ) );
  }
  if ( version > DRW::AC1015 )  //2004+
  {
    int t = buf->getBitShort();
    QgsDebugMsg( QString( "ShadePlot Mode...: %1" ).arg( t ) );
    Q_UNUSED( t );
  }
  if ( version > DRW::AC1018 )  //2007+
  {
    int t;
    t = buf->getBit();
    QgsDebugMsg( QString( "Use def Light...: %1" ).arg( t ) );
    Q_UNUSED( t );

    t = buf->getRawChar8();
    QgsDebugMsg( QString( "Def light tipe?: %1" ).arg( t ) );

    double d = buf->getBitDouble();
    QgsDebugMsg( QString( "Brightness: %1" ).arg( d ) );
    Q_UNUSED( d );

    d = buf->getBitDouble();
    QgsDebugMsg( QString( "Contrast: %1" ).arg( d ) );

    t = buf->getEnColor( version, color24, transparency );
    QgsDebugMsg( QString( "Ambient (Cmc or Enc?), Enc: 0x%1" ).arg( buf->getEnColor( version, color24, transparency ), 0, 16 ) );
  }
  ret = DRW_Entity::parseDwgEntHandle( version, buf );

  dwgHandle someHdl;
  if ( version < DRW::AC1015 )  //R13 & R14 only
  {
    QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "viewport ent handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
  }
  if ( version > DRW::AC1014 )  //2000+
  {
    for ( duint8 i = 0; i < frozenLyCount; ++i )
    {
      someHdl = buf->getHandle();
      QgsDebugMsg( QString( "frozen layer handle %1: %2.%3 0x%4" ).arg( i ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    }

    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "clip boundary handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );

    if ( version == DRW::AC1015 )  //2000 only
    {
      someHdl = buf->getHandle();
      QgsDebugMsg( QString( "viewport ent handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    }
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "named ucs handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "base ucs handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
  }
  if ( version > DRW::AC1018 )  //2007+
  {
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "background handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "visual style handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "shadeplot id handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
    QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );
    someHdl = buf->getHandle();
    QgsDebugMsg( QString( "SUN handle: %1.%2 0x%3" ).arg( someHdl.code ).arg( someHdl.size ).arg( someHdl.ref, 0, 16 ) );
  }

  QgsDebugMsg( QString( "Remaining bytes: %1" ).arg( buf->numRemainingBytes() ) );

  if ( !ret )
    return ret;

  return buf->isGood();
}
