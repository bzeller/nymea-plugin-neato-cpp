/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *                                                                         *
 *  Copyright (C) 2024 Benjamin Zeller <zeller.benjamin@web.de>            *
 *                                                                         *
 *  This library is free software; you can redistribute it and/or          *
 *  modify it under the terms of the GNU Lesser General Public             *
 *  License as published by the Free Software Foundation; either           *
 *  version 2.1 of the License, or (at your option) any later version.     *
 *                                                                         *
 *  This library is distributed in the hope that it will be useful,        *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU      *
 *  Lesser General Public License for more details.                        *
 *                                                                         *
 *  You should have received a copy of the GNU Lesser General Public       *
 *  License along with this library; If not, see                           *
 *  <http://www.gnu.org/licenses/>.                                        *
 *                                                                         *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include "neato.h"
#include "extern-plugininfo.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QTimer>

template<bool flag = false> void constexpr static_no_match() { static_assert(flag, "Static match failed"); }

/*!
  Small helper function to extract data from QJsonObject's auto converting to the target type
*/
template<typename T>
bool fetchElem ( const QJsonObject &o, const QString &field, T &destination ){
  const QJsonValue &v = o.value( field );
  if ( v.isUndefined() ) {
    qDebug(dcNeato()) << "Robot object misses field: "<< field;
    return false;
  }

  if constexpr ( std::is_same_v<QString, T> ) {
    destination = o.value(field).toString();
    return true;
  } else if constexpr ( std::is_same_v<QDateTime, T> ) {
    destination = QDateTime::fromString( o.value(field).toString(), Qt::ISODate );
    return destination.isValid();
  } else {
    static_no_match();
    // unreachable
    return false;
  }
};


Neato::Neato( NetworkAccessManager &nwAccess, const QByteArray &clientId, const QByteArray &clientSecret, QObject *parent )
  : QObject{parent}
  , m_networkManager( &nwAccess )
  , m_tokenTimeout( new QTimer(this) )
  , m_clientId( clientId )
  , m_clientSecret( clientSecret )
  , m_redirectUri( QByteArrayLiteral("https://127.0.0.1:8888") )
{
  m_tokenTimeout->setSingleShot(true);
  connect( m_tokenTimeout, &QTimer::timeout, this, [this](){
    qCDebug(dcNeato) << "Refresh authentication token";
    this->fetchAcessTokenFromRefreshToken( this->m_refreshToken );
  });
}

Neato::~Neato()
{
}

QUrl Neato::loginUrl() const
{
    // Compose the OAuth url. Make sure to start the callback/redirect URL with https://127.0.0.1
    QUrl url("https://apps.neatorobotics.com/oauth2/authorize");
    QUrlQuery queryParams;
    queryParams.addQueryItem("client_id", m_clientId);
    queryParams.addQueryItem("redirect_uri", m_redirectUri.toPercentEncoding() );
    queryParams.addQueryItem("response_type", "code");
    queryParams.addQueryItem("scope", "public_profile control_robots maps");
    queryParams.addQueryItem("state", "ya-ya"); // should we do this?
    url.setQuery(queryParams);
    return url;
}

QString Neato::accessToken()
{
    return m_accessToken;
}

QString Neato::refreshToken()
{
    return m_refreshToken;
}

void Neato::handleTokenReply( QNetworkReply *reply )
{
  QJsonDocument jsonDoc = QJsonDocument::fromJson(reply->readAll());
  const auto &variantMap = jsonDoc.toVariant().toMap();

  int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (status != 200 || reply->error() != QNetworkReply::NoError) {
      if(variantMap.contains("error_description")) {
          qWarning(dcNeato()) << "Access token error:" << variantMap.value("error_description").toString();
      }
      setState( State::Disconnected );
      emit authenticated(false);
      return;
  }


  if ( !variantMap.contains("access_token") || !variantMap.contains("refresh_token")) {
      qWarning(dcNeato()) << "Auth error: token missing from answer";
      setState( State::Disconnected );
      emit authenticated(false);
      return;
  }
  // If successful, extract the tokens
  this->m_accessToken  = variantMap.value("access_token").toByteArray();
  this->m_refreshToken = variantMap.value("refresh_token").toByteArray();

  if ( variantMap.contains("expires_in") ) {
    int expiryTime = variantMap.value("expires_in").toInt();
    qCDebug(dcNeato()) << "Access token expires at" << QDateTime::currentDateTime().addSecs(expiryTime).toString();
    if ( this->m_tokenTimeout )
      this->m_tokenTimeout->start( std::chrono::seconds(std::max(0, expiryTime - 20)) );
    else
      qWarning(dcNeato()) << "Token refresh timer not initialized";
  }
  setState( State::Connected );
  emit authenticated(true);
}

void Neato::fetchAcessTokenFromAuthorizationCode( const QString &authorizationCode )
{
  setState( State::Authenticating );
  QUrl url = beehiveRequestUrl("/oauth2/token");

  qCDebug(dcNeato()) << "Requesting new token via authorizationCode mechanism: " << authorizationCode;

  QUrlQuery query;
  query.addQueryItem("code", authorizationCode);
  query.addQueryItem("client_id", m_clientId);
  query.addQueryItem("client_secret", m_clientSecret);
  query.addQueryItem("grant_type", "authorization_code");
  query.addQueryItem("redirect_uri", m_redirectUri.toPercentEncoding());
  url.setQuery(query);

  QNetworkRequest request(url);

  // Send the request
  QNetworkReply *reply = m_networkManager->post(request, QByteArray());
  connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
  connect(reply, &QNetworkReply::finished, this, [this, reply](){
    handleTokenReply(reply);
  });
}

void Neato::fetchAcessTokenFromRefreshToken( const QString &refreshToken )
{
    if (refreshToken.isEmpty()) {
        qWarning(dcNeato()) << "No refresh token given!";
        emit authenticated(false);
        return;
    }

    qCDebug(dcNeato()) << "Requesting new token via refreshToken mechanism: " << refreshToken;

    QUrl url = beehiveRequestUrl("/oauth2/token");

    QUrlQuery query;
    query.clear();
    query.addQueryItem("grant_type", "refresh_token");
    query.addQueryItem("refresh_token", refreshToken);
    query.addQueryItem("client_id", m_clientId);
    query.addQueryItem("client_secret", m_clientSecret);
    query.addQueryItem("redirect_uri", m_redirectUri.toPercentEncoding());

    const QString data = query.toString(QUrl::FullyEncoded);

    QNetworkRequest request(url);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    //QByteArray auth = QByteArray(m_clientId + ':' + m_clientSecret).toBase64(QByteArray::Base64Encoding | QByteArray::KeepTrailingEquals);
    //request.setRawHeader("Authorization", QString("Basic %1").arg(QString(auth)).toUtf8());

    QNetworkReply *reply = m_networkManager->post(request, data.toUtf8());
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    connect(reply, &QNetworkReply::finished, this, [this, reply](){
      handleTokenReply(reply);
    });
}

void Neato::loadRobots()
{
    QNetworkRequest request;
    request.setHeader   (QNetworkRequest::KnownHeaders::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/vnd.neato.beehive.v1+json");
    request.setRawHeader("Authorization", ("Bearer " + m_accessToken).toLatin1() );
    request.setUrl( beehiveRequestUrl("/users/me/robots") );

    QNetworkReply *reply = m_networkManager->get(request);
    qDebug(dcNeato()) << "Sending request" << request.url() << request.rawHeaderList() << request.rawHeader("Authorization");

    connect(reply, &QNetworkReply::finished, this, [reply, this] {
        reply->deleteLater();
        int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        // Check HTTP status code
        if (status != 200 || reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::HostNotFoundError) {
                emit connectionChanged(false);
            }
            if (status == 400 || status == 401) {
                emit authenticationStatusChanged(false);
            }

            // beehive gives us a JSON Object with the error info
            QJsonParseError error;
            QJsonDocument data = QJsonDocument::fromJson(reply->readAll(), &error);
            if (error.error != QJsonParseError::NoError) {
              qCWarning(dcNeato()) << "Request error:" << status << reply->errorString();
              return;
            }

            qCWarning(dcNeato()) << "Request error:" << status << reply->errorString() << " Beehive message: " << data.object().value("message").toString();
            return;
        }
        emit connectionChanged(true);
        emit authenticationStatusChanged(true);

        QJsonParseError error;
        QJsonDocument data = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            qDebug(dcNeato()) << "Robot list: Received invalid response";
            return;
        }

        /*
        Beehive API gives us a list of robot objects:
        [
          {
            "serial": "robot1",
            "prefix": "NSN",
            "name": "Robot 1",
            "model": "botvac-85",
            "secret_key": "04a0fbe6b1f..2572d",
            "purchased_at": "2014-01-02T12:00:00Z",
            "linked_at": "2014-01-02T12:00:00Z",
            "traits": []
          }
        ]
        */

        m_robots.clear();
        for ( const auto &elem : data.array() ) {
          if ( !elem.isObject() ) {
            qDebug(dcNeato()) << "Robot list: Ignoring non object element";
            continue;
          }
          Robot r;
          const QJsonObject &o = elem.toObject();
          if ( !fetchElem( o, "serial", r.serial ) )          continue;
          if ( !fetchElem( o, "prefix", r.prefix ) )          continue;
          if ( !fetchElem( o, "name", r.name ) )              continue;
          if ( !fetchElem( o, "model", r.model ) )            continue;
          if ( !fetchElem( o, "secret_key", r.secret_key ) )  continue;
          fetchElem( o, "linked_at", r.linked_at );
          fetchElem( o, "purchased_at", r.purchased_at );
          // ignoring the traits element for now
          m_robots.append( std::move(r) );
        }
        emit robotsLoaded( );
    });
}

const QVector<Neato::Robot> &Neato::robots() const
{
  return m_robots;
}

void Neato::setState(State newState)
{
  if ( m_state != newState ) {
    m_state = newState;
    emit stateChanged( m_state );
  }
}

QUrl Neato::beehiveRequestUrl(const QString &path) const
{
  static QByteArray baseBeehiveUrl = QByteArrayLiteral("https://beehive.neatocloud.com");
  QUrl url( baseBeehiveUrl );
  if ( path.length() )
    url.setPath(path);

  return url;
}

QUrl Neato::nucleoRequestUrl(const QString &path) const
{
  static QByteArray baseNucleoUrl  = QByteArrayLiteral("https://nucleo.neatocloud.com:4443");
  QUrl url( baseNucleoUrl );
  if ( path.length() )
    url.setPath(path);

  return url;
}
