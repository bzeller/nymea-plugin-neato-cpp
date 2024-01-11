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

#include "plugininfo.h"
#include "integrationpluginneato.h"
#include "neato.h"

#include <network/networkaccessmanager.h>

#include <QUrlQuery>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>

IntegrationPluginNeato::IntegrationPluginNeato()
{

}

void IntegrationPluginNeato::startPairing(ThingPairingInfo *info)
{

    ApiKey apiKey = apiKeyStorage()->requestKey("neato");

    if ( info->thingClassId() == accountThingClassId ) {

        const auto &thingId = info->thingId();
        Neato *n = nullptr;
        if ( m_neatoAccounts.contains( thingId ) ) {
            if ( !info->isReconfigure() ) {
                // TODO reset the Neato instance
                info->finish(Thing::ThingErrorSetupFailed, QT_TR_NOOP("Trying to initialize a existing thing"));
                return;
            }
            n = m_neatoAccounts[thingId];
        } else {
            n = new Neato( *hardwareManager()->networkManager(), apiKey.data("clientId"), apiKey.data("clientSecret"), this );

            // register this account ID
            m_neatoAccounts.insert( info->thingId(), n );
        }

        // Set the OAuth url to the info object
        info->setOAuthUrl(n->loginUrl());

        // And finish the startPairing procedure
        info->finish(Thing::ThingErrorNoError);
    } else {

        qCWarning(dcNeato()) << "Unhandled pairing metod!";
        info->finish(Thing::ThingErrorCreationMethodNotSupported);
    }
}

void IntegrationPluginNeato::confirmPairing(ThingPairingInfo *info, const QString &username, const QString &secret)
{
    Q_UNUSED(username)
    if ( info->thingClassId() == accountThingClassId ) {

        const auto &thingId = info->thingId();
        if ( !m_neatoAccounts.contains( thingId ) ) {
            info->finish(Thing::ThingErrorSetupFailed, QT_TR_NOOP("Can not confirm a pairing session for a unknown thing."));
            return;
        }

        // Extract the code from the callback URL
        qCDebug(dcNeato()) << "!!!!! Received secret: " << secret;
        QUrl url(secret);
        QUrlQuery query(url);

        QString accessCode = query.queryItemValue("code");

        Neato *n = m_neatoAccounts[thingId];
        connect(n, &Neato::authenticated, info, [this, n, info]( bool success ){
            if ( !success ) {
                info->finish(Thing::ThingErrorSetupFailed, QT_TR_NOOP("Authentication failed. Please try again."));
                return;
            } else {
                info->finish(Thing::ThingErrorNoError);
            }
        });
        n->fetchAcessTokenFromAuthorizationCode( accessCode );
    } else {
        qCWarning(dcNeato()) << "Invalid thingClassId -> no pairing possible with this device";
        info->finish(Thing::ThingErrorThingClassNotFound);
    }
}

void IntegrationPluginNeato::setupThing(ThingSetupInfo *info)
{
    qCDebug(dcNeato()) << "Setup thing" << info->thing()->name() << info->thing()->params();

    Thing *thing = info->thing();
    if ( thing->thingClassId() == accountThingClassId ) {

         qCDebug(dcNeato()) << "Setup Account Thing Type";

        auto finishSetup = [this, info]( bool authenticated ){

            if ( !m_neatoAccounts.contains(info->thing()->id()) ) {
                info->finish(Thing::ThingErrorAuthenticationFailure);
                return;
            }

            if ( !authenticated ) {
                delete m_neatoAccounts.take( info->thing()->id() );
                info->finish(Thing::ThingErrorAuthenticationFailure);
                return;
            }

            Thing *thing = info->thing();
            Neato *n = m_neatoAccounts[thing->id()];

            // Store refresh token in the plugin storage
            pluginStorage()->beginGroup(thing->id().toString());
            pluginStorage()->setValue("refreshToken", n->refreshToken() );
            pluginStorage()->endGroup();

            thing->setStateValue(accountLoggedInStateTypeId, true);
            thing->setStateValue(accountConnectedStateTypeId, true);

            info->finish(Thing::ThingErrorNoError);

            n->loadRobots();
            connect(n, &Neato::robotsLoaded, this, &IntegrationPluginNeato::robotsLoaded );
        };

        Neato *n = nullptr;
        const auto &thingId = thing->id();
        if ( m_neatoAccounts.contains(thingId) ) {
             qCDebug(dcNeato()) << "Thing Id was already known, finish thing setup";
            // freshly paired, we can just use the already existing instance
            n = m_neatoAccounts[thingId];
            finishSetup(true);
        } else {
            qCDebug(dcNeato()) << "Loading the refresh token and let'se gooo";
            // reloading the account from thing storage, refresh the token then finish setup
            ApiKey apiKey = apiKeyStorage()->requestKey("neato");
            n = new Neato( *hardwareManager()->networkManager(), apiKey.data("clientId"), apiKey.data("clientSecret"), this );

            // register this account ID
            m_neatoAccounts.insert( thingId, n );

            //thing loaded from the thing database, proactively query a refresh token
            pluginStorage()->beginGroup(thing->id().toString());
            QString refreshToken = pluginStorage()->value("refreshToken").toString();
            pluginStorage()->endGroup();

            if (refreshToken.isEmpty()) {
                 qCDebug(dcNeato()) << "What no refresh token? I give up";
                finishSetup(false);
                return;
            }

            connect(n, &Neato::authenticated, info, std::move(finishSetup) );
            n->fetchAcessTokenFromRefreshToken( refreshToken );
        }

        return;
    }

    if ( thing->thingClassId() == robotThingClassId ) {
        return info->finish(Thing::ThingErrorNoError);
    }

    qCWarning(dcNeato()) << "Unhandled thing class id in setupDevice" << thing->thingClassId();
    info->finish( Thing::ThingErrorSetupMethodNotSupported, "Unhandled thing class id in setupDevice" );
}

void IntegrationPluginNeato::executeAction(ThingActionInfo *info)
{
    qCDebug(dcNeato()) << "Executing action for thing" << info->thing()->name() << info->action().actionTypeId().toString() << info->action().params();

    info->finish(Thing::ThingErrorNoError);
}

void IntegrationPluginNeato::thingRemoved(Thing *thing)
{
    qCDebug(dcNeato()) << "Remove thing" << thing->name() << thing->params();

    // Clean up all data related to this thing
}

void IntegrationPluginNeato::robotsLoaded()
{
    Neato *n = qobject_cast<Neato *>(sender());
    if ( !n )
        return;

    ThingId accountThingId = m_neatoAccounts.key(n);

    Thing* accountThing = myThings().findById(accountThingId);
    if (!accountThing)
        return;

    const auto &robots = n->robots();
    {
        QList<ThingDescriptor> newRobots;

        // lets make sure all robots are registered
        for ( const auto &r : robots ) {
            Thing *robotThing = myThings().findByParams(ParamList() << Param(robotThingSerialParamTypeId, r.serial));
            if (robotThing) {
                if (robotThing->name() != r.name) {
                    qDebug(dcNeato()) << "Updating group name" << robotThing->name() << "to" << r.name;
                    robotThing->setName( r.name );
                }
                // make sure secret is up2date
                robotThing->setParamValue(robotThingSecretParamTypeId, r.secret_key );
            } else {
                //new thing, add to the system
                ThingDescriptor thingDescriptor(robotThingClassId, r.name, r.model, accountThingId );
                ParamList params;
                params.append(Param(robotThingSerialParamTypeId, r.serial));
                params.append(Param(robotThingSecretParamTypeId, r.secret_key));
                thingDescriptor.setParams(params);
                newRobots.append(thingDescriptor);
            }
        }

        if (!newRobots.isEmpty())
            emit autoThingsAppeared(newRobots);
    }

    // remove vanished devices
    for ( Thing *robotThing : myThings().filterByParentId(accountThingId) ) {
        QString robotSerial = robotThing->paramValue(robotThingSerialParamTypeId).toString();
        bool deviceFound = std::any_of( robots.begin(), robots.end(), [&robotSerial]( const auto &r ) { return r.serial == robotSerial; } );
        if (!deviceFound) {
            emit autoThingDisappeared(robotThing->id());
        }
    }
}
