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

#ifndef NEATO_H
#define NEATO_H


#include <network/networkaccessmanager.h>
#include <integrations/thing.h>

#include <QObject>
#include <QDateTime>
#include <QVector>
#include <optional>

class QTimer;
class QNetworkReply;

class Neato : public QObject
{
  Q_OBJECT
public:

  enum class State {
    Disconnected,
    Authenticating,
    Connected,
    InvalidToken
  };
  Q_ENUM(State)

  /*!
  Robot Info as returned by the Neato Beehive API
  */
  struct Robot {
    QString serial;
    QString prefix;
    QString name;
    QString model;
    QString secret_key;
    QDateTime purchased_at;
    QDateTime linked_at;
    QVector<QString> traits;
  };

  // codes as defined in https://developers.neatorobotics.com/api/robot-remote-protocol/request-response-formats
  enum class StateCode {
    Invalid,
    Idle,
    Busy,
    Paused,
    Error
  };

  enum class ActionCode {
    Invalid,
    HouseCleaning,
    SpotCleaning,
    ManualCleaning,
    Docking,
    UserMenuActive,
    SuspendedCleaning,
    Updating,
    CopyingLogs,
    RecoveringLocation,
    IEC_Test,
    MapCleaning,
    ExploringMap, //creating a persistent map
    AcquiringPersistentMapIDs,
    CreatingAndUploadingMap,
    SuspendedExploration
  };

  enum class CleaningCategory {
    Invalid,
    Manual,
    House,
    Spot,
    Map
  };

  enum class CleaningPerformance {
    Invalid,
    Eco,
    Turbo
  };

  enum class CleaningModifier {
    Invalid,
    Normal,
    Double
  };

  enum class NavigationMode {
    Invalid,
    Normal,
    ExtraCare,
    Deep
  };

  struct RobotState {
    StateCode state = StateCode::Invalid;    // Specifies the state of the Robot.
    ActionCode action = ActionCode::Invalid; //If the state is busy, this element specifies what the robot is or has been busy doing. If the state is pause or error, it specifies the activity that the Robot was doing. If state is other, this element is Invalid.
    QString error; // Specifies the current error state in the robot. Errors are defined as a blocking condition that requires user intervention. Errors can only be cleared on the robot, with the user pressing a button.
    QString alert; // Specifies the current alert state in the robot. Alerts are defined as a non-blocking condition that does not require user intervention. Alerts can be cleared on the apps, issuing the appropriate call dimissCurrentAlert.

    // Provides additional information on the current or last cleaning settings. These params SHOULD be used by the apps to set the defaults cleaning settings.
    struct {
      CleaningCategory category = CleaningCategory::Invalid;
      CleaningPerformance mode  = CleaningPerformance::Invalid;
      CleaningModifier modifier = CleaningModifier::Invalid;
      std::optional<NavigationMode> navigationMode;
      std::optional<int> spotWidth;
      std::optional<int> spotHeight;
    } cleaning;

    struct {
      bool isCharging = false;
      bool isDocked   = false;
      bool dockHasBeenSeen = false;
      int charge = 0;  // charge percentage 0-100
      bool isScheduleEnabled = false;
    } details;

    // The commands that a Robot will accept.
    struct {
      bool start = false;
      bool stop  = false;
      bool pause = false;
      bool resume = false;
      bool goToBase = false;
    } availableCommands;

  };

  explicit Neato( NetworkAccessManager &nwAccess, const QByteArray &clientId, const QByteArray &clientSecret, QObject *parent = nullptr );
  ~Neato();

  QUrl loginUrl() const;

  void fetchAcessTokenFromAuthorizationCode(const QString &authorizationCode);
  QString accessToken();

  void fetchAcessTokenFromRefreshToken(const QString &refreshToken);
  QString refreshToken();

  void loadRobots();
  const QVector<Robot> &robots() const;

  void pollRobotState( const QString &robotSerial );

private slots:
  void handleTokenReply( QNetworkReply *reply );

private:
  void setState( State newState );
  QUrl beehiveRequestUrl ( const QString &path = QString() ) const;
  QUrl nucleoRequestUrl  ( const QString &path = QString() ) const;

signals:
  void stateChanged ( State state );

  // signal only emitted once we managed to get a valid access token, either via accessCode or refreshToken
  void authenticated( bool authenticated );

  void robotsLoaded();


  void connectionChanged( bool connected );
  void authenticationStatusChanged( bool authenticated );

private:
  State m_state = State::Disconnected;
  NetworkAccessManager *m_networkManager = nullptr;
  QTimer *m_tokenTimeout = nullptr;

  // OAuth information:
  QByteArray m_clientId;
  QByteArray m_clientSecret;
  QString m_accessToken;
  QString m_refreshToken;
  QByteArray m_redirectUri;

  // neato data
  QVector<Robot> m_robots;
};

#endif // NEATO_H
